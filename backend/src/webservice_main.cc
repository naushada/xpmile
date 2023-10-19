#ifndef __webservice_main_cc__
#define __webservice_main_cc__

#include "webservice.h"
#include "emailservice.hpp"

int main(int argc, char* argv[])
{
   std::string ip("");
   std::string port("");
   std::string worker("");
   std::string db_uri("");
   std::string db_conn_pool("");
   std::string db_name("");

   std::array<std::string, std::size_t(CommandArgumentName::MAX_CMD_ARG)> cmdOpt;

   int _port = 8080;
   int _worker = 10;
   /* mongodb connection pool */
   int _pool = 50;

   cmdOpt.fill("");

   ACE_LOG_MSG->open (argv[0], ACE_LOG_MSG->STDERR|ACE_LOG_MSG->SYSLOG);

   /* The last argument tells from where to start in argv - offset of argv array */
   ACE_Get_Opt opts (argc, argv, ACE_TEXT ("s:p:w:u:c:h:d:n:i:o:"), 1);

   opts.long_option(ACE_TEXT("server-ip"), 's', ACE_Get_Opt::ARG_REQUIRED);
   opts.long_option(ACE_TEXT("server-port"), 'p', ACE_Get_Opt::ARG_REQUIRED);
   opts.long_option(ACE_TEXT("server-worker"), 'w', ACE_Get_Opt::ARG_REQUIRED);
   opts.long_option(ACE_TEXT("mongo-db-uri"), 'u', ACE_Get_Opt::ARG_REQUIRED);
   opts.long_option(ACE_TEXT("mongo-db-connection-pool"), 'c', ACE_Get_Opt::ARG_REQUIRED);
   opts.long_option(ACE_TEXT("mongo-db-name"), 'd', ACE_Get_Opt::ARG_REQUIRED);
   /* email client configuration */
   opts.long_option(ACE_TEXT("email-from-name"), 'n', ACE_Get_Opt::ARG_REQUIRED);
   opts.long_option(ACE_TEXT("email-from-id"), 'i', ACE_Get_Opt::ARG_REQUIRED);
   opts.long_option(ACE_TEXT("email-from-password"), 'o', ACE_Get_Opt::ARG_REQUIRED);

   opts.long_option(ACE_TEXT("help"), 'h', ACE_Get_Opt::ARG_REQUIRED);

   int c = 0;

   while ((c = opts ()) != EOF) {
     switch (c) {
       case 's':
       {
         cmdOpt[std::size_t(CommandArgumentName::SERVER_IP)] = std::string(opts.opt_arg());
         ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [master:%t] %M %N:%l IP %s\n"), std::get<std::size_t(CommandArgumentName::SERVER_IP)>(cmdOpt).c_str()));
       }
         break;

       case 'p':
       {
         cmdOpt[std::size_t(CommandArgumentName::SERVER_PORT)] = std::string(opts.opt_arg());
         ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [master:%t] %M %N:%l PORT %s\n"), std::get<std::size_t(CommandArgumentName::SERVER_PORT)>(cmdOpt).c_str()));
       }
         break;

       case 'w':
       {
         cmdOpt[std::size_t(CommandArgumentName::SERVER_WORKER_NODE)] = std::string(opts.opt_arg());
         ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [master:%t] %M %N:%l Worker Nodes %s\n"), std::get<std::size_t(CommandArgumentName::SERVER_WORKER_NODE)>(cmdOpt).c_str()));
       }
         break;

       case 'u':
         cmdOpt[std::size_t(CommandArgumentName::DB_URI)] = std::string(opts.opt_arg());
         ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [master:%t] %M %N:%l Database URI %s\n"), std::get<std::size_t(CommandArgumentName::DB_URI)>(cmdOpt).c_str()));
         break;
         
       case 'c':
         cmdOpt[std::size_t(CommandArgumentName::DB_CONN_POOL)] = std::string(opts.opt_arg());
         ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [master:%t] %M %N:%l Database Connection Pool %s\n"), std::get<std::size_t(CommandArgumentName::DB_CONN_POOL)>(cmdOpt).c_str()));
         break;

       case 'd':
         cmdOpt[std::size_t(CommandArgumentName::DB_NAME)] = std::string(opts.opt_arg());
         ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [master:%t] %M %N:%l Database Name %s\n"), std::get<std::size_t(CommandArgumentName::DB_NAME)>(cmdOpt).c_str()));
         break;

       case 'n':
         cmdOpt[std::size_t(CommandArgumentName::EMAIL_FROM_NAME)] = std::string(opts.opt_arg());
         ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [master:%t] %M %N:%l email from name %s\n"), std::get<std::size_t(CommandArgumentName::EMAIL_FROM_NAME)>(cmdOpt).c_str()));
         break;

       case 'i':
         cmdOpt[std::size_t(CommandArgumentName::EMAIL_FROM_ID)] = std::string(opts.opt_arg());
         ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [master:%t] %M %N:%l email from email-id %s\n"), std::get<std::size_t(CommandArgumentName::EMAIL_FROM_ID)>(cmdOpt).c_str()));
         break;

       case 'o':
         cmdOpt[std::size_t(CommandArgumentName::EMAIL_FROM_PASSWORD)] = std::string(opts.opt_arg());
         ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [master:%t] %M %N:%l email from emai-password %s\n"), std::get<std::size_t(CommandArgumentName::EMAIL_FROM_PASSWORD)>(cmdOpt).c_str()));
         break;

       case 'h':
       default:
          ACE_ERROR_RETURN ((LM_ERROR,
                            ACE_TEXT("%D [Master:%t] %M %N:%l usage: %s\n"
                            " [-s --server-ip]\n"
                            " [-p --server-port]\n"
                            " [-w --server-worker]\n"
                            " [-u --mongo-db-uri (mongodb://127.0.0.1:27017)]\n"
                            " [-c --mongo-db-connection-pool]\n"
                            " [-d --mongo-db-name]\n"
                            " [-h --help]\n"
                            ),
                            argv [0]),
                            -1);
     }
   }

   if(std::get<std::size_t(CommandArgumentName::SERVER_PORT)>(cmdOpt).length()) {
     _port = std::stoi(std::get<std::size_t(CommandArgumentName::SERVER_PORT)>(cmdOpt));
     ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l listening port %d\n"), _port));

   }

   if(std::get<std::size_t(CommandArgumentName::SERVER_WORKER_NODE)>(cmdOpt).length()) {
     _worker = std::stoi(std::get<std::size_t(CommandArgumentName::SERVER_WORKER_NODE)>(cmdOpt));
     ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l the number of worker node is %d\n"), _worker));
   }
   
   if(std::get<std::size_t(CommandArgumentName::DB_CONN_POOL)>(cmdOpt).length()) {
     _pool = std::stoi(std::get<std::size_t(CommandArgumentName::DB_CONN_POOL)>(cmdOpt));
     ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l the mongodb connection pool is %d\n"), _pool));
   } 

   WebServer inst(std::get<std::size_t(CommandArgumentName::SERVER_IP)>(cmdOpt),
                  _port, _worker, 
                  std::get<std::size_t(CommandArgumentName::DB_URI)>(cmdOpt),
                  std::get<std::size_t(CommandArgumentName::DB_CONN_POOL)>(cmdOpt),
                  std::get<std::size_t(CommandArgumentName::DB_NAME)>(cmdOpt));

   /* filling email for sending updateds */
    
   SMTP::Account::instance().from_name(std::get<std::size_t(CommandArgumentName::EMAIL_FROM_NAME)>(cmdOpt));
   SMTP::Account::instance().from_email(std::get<std::size_t(CommandArgumentName::EMAIL_FROM_ID)>(cmdOpt));
   SMTP::Account::instance().from_password(std::get<std::size_t(CommandArgumentName::EMAIL_FROM_PASSWORD)>(cmdOpt));
   
   ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [Master:%t] %M %N:%l from_name:%s\n"), SMTP::Account::instance().from_name().c_str()));

   inst.start();

   return(0);
}

















#endif /* __webservice_main_cc__*/
