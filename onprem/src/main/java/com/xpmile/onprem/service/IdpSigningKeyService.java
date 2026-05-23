package com.xpmile.onprem.service;

import com.mongodb.client.MongoClient;
import com.mongodb.client.MongoCollection;
import com.mongodb.client.model.Filters;
import com.mongodb.client.model.Sorts;
import com.mongodb.client.model.Updates;
import org.bson.Document;
import org.bson.types.ObjectId;
import org.springframework.stereotype.Service;

import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.SecureRandom;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Base64;
import java.util.List;

/**
 * Manages {@code idp.idp_signing_keys} — the RSA-2048 keypairs the in-house
 * IdP signs id_tokens with. Read directly by the on-prem wsdbagent's
 * {@code sign_jwt_on_prem.cpp}: the cloud never sees private key bytes.
 *
 * One document per key:
 * <pre>{@code
 * {
 *   _id:          ObjectId,
 *   kid:          "k-<8 hex chars>",          // header.kid value
 *   alg:          "RS256",
 *   publicKeyPem: "-----BEGIN PUBLIC KEY-----…",
 *   privateKeyPem:"-----BEGIN PRIVATE KEY-----…",   // never leaves on-prem
 *   active:       true | false,                     // exactly one true at a time
 *   createdAt:    epoch-seconds,
 *   notAfter:     epoch-seconds | 0                 // 0 = never expires
 * }
 * }</pre>
 *
 * Activation is mutually exclusive — {@link #activate(String)} flips one
 * row to {@code active:true} and every other row to {@code active:false}
 * in a single atomic update_many. The cloud-side {@code IdpJwks} endpoint
 * publishes every non-expired key (so clients caching JWKS continue to
 * verify in-flight tokens after a rotation), but {@code sign_jwt_on_prem}
 * signs only with the {@code active:true} row.
 *
 * <p>Lives in the {@code idp} database (not {@code xpmile}) — design's
 * dual-database split.
 */
@Service
public class IdpSigningKeyService {

    /** Hardcoded — the IdP database is by design separate from xpmile. */
    private static final String IDP_DATABASE = "idp";
    private static final String COLLECTION   = "idp_signing_keys";

    private static final String ALG_RS256          = "RS256";
    private static final int    RSA_KEY_BITS       = 2048;
    private static final int    KID_RANDOM_BYTES   = 4;       // 8 hex chars
    private static final SecureRandom RAND = new SecureRandom();

    private final MongoCollection<Document> collection;

    public IdpSigningKeyService(MongoClient mongoClient) {
        this.collection = mongoClient.getDatabase(IDP_DATABASE).getCollection(COLLECTION);
    }

    /** List every key, newest first. The {@code privateKeyPem} field is
     *  stripped from the result — the UI never displays it. */
    public List<Document> list() {
        List<Document> out = new ArrayList<>();
        for (Document doc : collection.find().sort(Sorts.descending("createdAt"))) {
            doc.remove("privateKeyPem");
            out.add(doc);
        }
        return out;
    }

    /** Generate a new RSA-2048 keypair and persist it. Returns the new
     *  document (with {@code privateKeyPem} already stripped). */
    public Document generate(boolean activateImmediately) {
        try {
            KeyPairGenerator kpg = KeyPairGenerator.getInstance("RSA");
            kpg.initialize(RSA_KEY_BITS);
            KeyPair kp = kpg.generateKeyPair();

            String kid          = "k-" + randomHex(KID_RANDOM_BYTES);
            String privatePem   = pemEncode("PRIVATE KEY", kp.getPrivate().getEncoded());
            String publicPem    = pemEncode("PUBLIC KEY",  kp.getPublic().getEncoded());
            long   createdAt    = Instant.now().getEpochSecond();

            Document doc = new Document("_id", new ObjectId())
                    .append("kid",           kid)
                    .append("alg",           ALG_RS256)
                    .append("publicKeyPem",  publicPem)
                    .append("privateKeyPem", privatePem)
                    .append("active",        false)
                    .append("createdAt",     createdAt)
                    .append("notAfter",      0L);
            collection.insertOne(doc);

            if (activateImmediately) {
                activate(kid);
            }
            doc.remove("privateKeyPem");
            // Re-read so the active flag reflects what's actually in the
            // collection after the (possible) activation step above.
            return collection.find(Filters.eq("kid", kid))
                    .projection(new Document("privateKeyPem", 0))
                    .first();
        } catch (Exception e) {
            throw new RuntimeException("RSA keypair generation failed", e);
        }
    }

    /** Make {@code kid} the sole active key. Returns true if a row was
     *  actually toggled to active (false if {@code kid} doesn't exist).
     *  Two writes — never an in-between state where zero rows are
     *  active (we set active:true on the target first, then clear the
     *  others). */
    public boolean activate(String kid) {
        var r1 = collection.updateOne(
                Filters.eq("kid", kid),
                Updates.set("active", true));
        if (r1.getMatchedCount() == 0) return false;

        collection.updateMany(
                Filters.and(Filters.ne("kid", kid), Filters.eq("active", true)),
                Updates.set("active", false));
        return true;
    }

    /** Hard delete a key by kid. Use carefully — the cloud-side JWKS
     *  refresh poll will drop it, but any token already issued under
     *  this kid will fail verification at the RP until its TTL expires
     *  (and its access_token / id_token can't be /userinfo'd or
     *  re-verified). For graceful rotation, set {@code notAfter} on the
     *  old key + activate the new one rather than deleting. */
    public boolean delete(String kid) {
        var r = collection.deleteOne(Filters.eq("kid", kid));
        return r.getDeletedCount() > 0;
    }

    /** Set an expiry timestamp (epoch-seconds) on a key — the cloud-
     *  side JWKS endpoint stops publishing it once {@code now >= notAfter}.
     *  Pass 0 to clear the expiry. */
    public boolean setNotAfter(String kid, long notAfterEpochSec) {
        var r = collection.updateOne(
                Filters.eq("kid", kid),
                Updates.set("notAfter", notAfterEpochSec));
        return r.getMatchedCount() > 0;
    }

    // ── PEM helpers ──────────────────────────────────────────────────────

    /** PEM-encode raw DER bytes with the given label
     *  (e.g. "PRIVATE KEY", "PUBLIC KEY"). 64-char-per-line base64 body
     *  matches what `PEM_read_bio_PrivateKey` / `PEM_read_bio_PUBKEY`
     *  on the wsdbagent / cloud side expect. */
    private static String pemEncode(String label, byte[] der) {
        String b64 = Base64.getMimeEncoder(64, "\n".getBytes()).encodeToString(der);
        return "-----BEGIN " + label + "-----\n"
             + b64 + "\n"
             + "-----END " + label + "-----\n";
    }

    private static String randomHex(int nBytes) {
        byte[] b = new byte[nBytes];
        RAND.nextBytes(b);
        StringBuilder sb = new StringBuilder(nBytes * 2);
        for (byte x : b) sb.append(String.format("%02x", x));
        return sb.toString();
    }
}
