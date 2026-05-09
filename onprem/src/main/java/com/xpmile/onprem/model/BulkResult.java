package com.xpmile.onprem.model;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;

@JsonIgnoreProperties(ignoreUnknown = true)
public class BulkResult {
    private int created;
    private int failed;

    public BulkResult() {}

    public BulkResult(int created, int failed) {
        this.created = created;
        this.failed = failed;
    }

    public int getCreated() { return created; }
    public void setCreated(int created) { this.created = created; }

    public int getFailed() { return failed; }
    public void setFailed(int failed) { this.failed = failed; }
}
