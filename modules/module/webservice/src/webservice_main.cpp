#include "emailservice.hpp"
#include "webservice.hpp"

namespace {

using Arg = CommandArgumentName;
constexpr std::size_t idx(Arg a) { return static_cast<std::size_t>(a); }
constexpr std::size_t N = idx(Arg::MAX_CMD_ARG);

int opt_int(const std::array<std::string, N> &opt, Arg key, int default_val) {
  const auto &s = opt[idx(key)];
  return s.empty() ? default_val : std::stoi(s);
}

void print_usage(const char *prog) {
  ACE_ERROR((LM_ERROR,
             ACE_TEXT("Usage: %s [OPTIONS]\n\n"
                      "  --server-ip              <addr>  Bind address (default: all interfaces)\n"
                      "  --server-port            <n>     Listen port            (default: 8080)\n"
                      "  --server-worker          <n>     Worker thread count    (default: 10)\n"
                      "  --mongo-db-uri           <uri>   MongoDB connection URI\n"
                      "  --mongo-db-connection-pool <n>   Connection pool size   (default: 50)\n"
                      "  --mongo-db-name          <name>  Database name\n"
                      "  --email-from-name        <name>  Outgoing email display name\n"
                      "  --email-from-id          <addr>  Outgoing email address\n"
                      "  --email-from-password    <pw>    Outgoing email password\n"
                      "  --help                           Show this help\n"),
             prog));
}

} // namespace

int main(int argc, char *argv[]) {
  ACE_LOG_MSG->open(argv[0], ACE_LOG_MSG->STDERR | ACE_LOG_MSG->SYSLOG);
  ACE_LOG_MSG->priority_mask(LM_CRITICAL | LM_ERROR | LM_DEBUG,
                             ACE_Log_Msg::PROCESS);

  std::array<std::string, N> opt{};

  ACE_Get_Opt args(argc, argv, ACE_TEXT("s:p:w:u:c:d:n:i:o:h"), 1);
  args.long_option(ACE_TEXT("server-ip"),               's', ACE_Get_Opt::ARG_REQUIRED);
  args.long_option(ACE_TEXT("server-port"),             'p', ACE_Get_Opt::ARG_REQUIRED);
  args.long_option(ACE_TEXT("server-worker"),           'w', ACE_Get_Opt::ARG_REQUIRED);
  args.long_option(ACE_TEXT("mongo-db-uri"),            'u', ACE_Get_Opt::ARG_REQUIRED);
  args.long_option(ACE_TEXT("mongo-db-connection-pool"),'c', ACE_Get_Opt::ARG_REQUIRED);
  args.long_option(ACE_TEXT("mongo-db-name"),           'd', ACE_Get_Opt::ARG_REQUIRED);
  args.long_option(ACE_TEXT("email-from-name"),         'n', ACE_Get_Opt::ARG_REQUIRED);
  args.long_option(ACE_TEXT("email-from-id"),           'i', ACE_Get_Opt::ARG_REQUIRED);
  args.long_option(ACE_TEXT("email-from-password"),     'o', ACE_Get_Opt::ARG_REQUIRED);
  args.long_option(ACE_TEXT("help"),                    'h', ACE_Get_Opt::NO_ARG);

  // Short-option char → enum key table
  static constexpr std::pair<char, Arg> kOptMap[] = {
    {'s', Arg::SERVER_IP},
    {'p', Arg::SERVER_PORT},
    {'w', Arg::SERVER_WORKER_NODE},
    {'u', Arg::DB_URI},
    {'c', Arg::DB_CONN_POOL},
    {'d', Arg::DB_NAME},
    {'n', Arg::EMAIL_FROM_NAME},
    {'i', Arg::EMAIL_FROM_ID},
    {'o', Arg::EMAIL_FROM_PASSWORD},
  };

  int c;
  while ((c = args()) != EOF) {
    if (c == 'h') { print_usage(argv[0]); return 0; }
    if (c == '?') { print_usage(argv[0]); return -1; }

    for (auto &[ch, key] : kOptMap) {
      if (c == ch) {
        opt[idx(key)] = args.opt_arg();
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [WebServer:%t] %M %N:%l opt -%c = %s\n"),
                   c, opt[idx(key)].c_str()));
        break;
      }
    }
  }

  const int port   = opt_int(opt, Arg::SERVER_PORT,        8080);
  const int worker = opt_int(opt, Arg::SERVER_WORKER_NODE, 10);

  ACE_DEBUG((LM_DEBUG,
             ACE_TEXT("%D [WebServer:%t] %M %N:%l "
                      "port:%d workers:%d db-pool:%s db-name:%s\n"),
             port, worker,
             opt[idx(Arg::DB_CONN_POOL)].c_str(),
             opt[idx(Arg::DB_NAME)].c_str()));

  WebServer inst(opt[idx(Arg::SERVER_IP)], port, worker,
                 opt[idx(Arg::DB_URI)],
                 opt[idx(Arg::DB_CONN_POOL)],
                 opt[idx(Arg::DB_NAME)]);

  SMTP::Account::instance().from_name(opt[idx(Arg::EMAIL_FROM_NAME)]);
  SMTP::Account::instance().from_email(opt[idx(Arg::EMAIL_FROM_ID)]);
  SMTP::Account::instance().from_password(opt[idx(Arg::EMAIL_FROM_PASSWORD)]);

  inst.start();
  return 0;
}
