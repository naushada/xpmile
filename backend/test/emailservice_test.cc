#ifndef __emailservice_test_cc__
#define __emailservice_test_cc__

#include "emailservice_test.hpp"

void EmailServiceTest::SetUp()
{
    /* Mongo DB interface */
    std::string uri("mongodb://127.0.0.1:27017");
    std::string _dbName("bayt");
    std::uint32_t _pool = 50;

    ACE_UNUSED_ARG(_pool);
    ACE_UNUSED_ARG(_dbName);

    mMongodbc = std::make_unique<MongodbClient>(uri);
    mUser = std::make_unique<SMTP::User>();

}

void EmailServiceTest::TearDown()
{
    mMongodbc.release();
    mUser.release();
}

void EmailServiceTest::TestBody()
{

}

EmailServiceTest::EmailServiceTest(std::string in)
{
    // {"subject": "", "to": [user-id@domain.com, user-id1@domain.com], "body": ""}
        /* Check for Query string */
    
    std::string json_body = in;
    std::vector<std::string> out_vec;
    std::vector<std::tuple<std::string, std::string>> out_list;
    std::string subj;
    std::string body;

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email request:%s\n"), json_body.c_str()));
    mMongodbc->from_json_array_to_vector(json_body, "to", out_vec);
    mMongodbc->from_json_element_to_string(json_body, "subject", subj);
    mMongodbc->from_json_element_to_string(json_body, "body", body);
    mMongodbc->from_json_object_to_map(json_body, "files", out_list);
    for(const auto& elm: out_vec) {
        ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email to list:%s\n"), elm.c_str()));
    }

    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email subject:%s\n"), subj.c_str()));
    ACE_DEBUG((LM_DEBUG, ACE_TEXT("%D [worker:%t] %M %N:%l email body:%s\n"), body.c_str()));

    SMTP::Account::instance().to_email(out_vec);
    SMTP::Account::instance().email_subject(subj);
    SMTP::Account::instance().email_body(body);

    if(!out_list.empty()) {
        /* e-mail with attachment */
        SMTP::Account::instance().attachment(out_list);
    }
}

// Demonstrate some basic assertions.
TEST(Test1, BasicAssertions) {
  std::stringstream ss("");
  ss << "{\"subject\": \"Test email client\", "
     << "\"to\": [\"naushad.dln@gmail.com\", \"hnm.royal@gmail.com\", \"dream.avenue.gp@gmail.com\"],"
     << "\"body\": \"abcd\","
     << "\"files\": [{\"file-name\": \"/home/mnahmed/PWI_264_Jul.pdf\", \"file-content\":\"testing\"}]"
     << "}";
  std::stringstream response("");
  response << "250-smtp.gmail.com at your service, [45.252.71.217]\r\n"
           << "250-SIZE 35882577\r\n"
           << "250-8BITMIME\r\n"
           << "250-STARTTLS\r\n"
           << "250-ENHANCEDSTATUSCODES\r\n"
           << "250-PIPELINING\r\n"
           << "250-CHUNKING\r\n"
           << "250 SMTPUTF8\r\n";
   
  EmailServiceTest emailTest(ss.str());
  emailTest.mUser->fsm().set_state(SMTP::GREETING());
  emailTest.mUser->rx(response.str());
  // Expect two strings not to be equal.
  EXPECT_STRNE("hello", "world");
  // Expect equality.
  EXPECT_EQ(7 * 6, 42);
}


#endif /*__emailservice_test_cc__*/