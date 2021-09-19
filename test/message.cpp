#include "wsudo/message.h"

#include <catch2/catch_all.hpp>

using namespace wsudo::msg;
using namespace std::string_view_literals;

TEST_CASE("Message defaults to invalid", "[message]") {
  Message m;
  REQUIRE(std::holds_alternative<Invalid>(m));
}

TEST_CASE("Empty buffer deserializes to invalid", "[message]") {
  Message m{std::string_view{}};
  REQUIRE(std::holds_alternative<Invalid>(m));
}

TEST_CASE("Unknown code deserializes to invalid", "[message]") {
  Message m{"XXXX"sv};
  REQUIRE(std::holds_alternative<Invalid>(m));
}

TEST_CASE("QSES missing params deserializes to invalid", "[message]") {
  Message m1{"QSES"sv};
  Message m2{"QSESdomain"sv};
  Message m3{"QSES\\"sv};
  Message m4{"QSESx\\"sv};

  REQUIRE(std::holds_alternative<Invalid>(m1));
  REQUIRE(std::holds_alternative<Invalid>(m2));
  REQUIRE(std::holds_alternative<Invalid>(m3));
  REQUIRE(std::holds_alternative<Invalid>(m4));
}

TEST_CASE("QSES allows empty domain", "[message]") {
  Message m{"QSES\\user"sv};
  REQUIRE(std::holds_alternative<QuerySession>(m));
  REQUIRE(std::get<QuerySession>(m).domain.empty());
  REQUIRE(std::get<QuerySession>(m).username == "user"sv);
}

TEST_CASE("CRED missing params deserializes to invalid", "[message]") {
  Message m1{"CRED"sv};
  Message m2{"CREDdomain"sv};
  Message m3{"CREDd\\"sv};
  Message m4{"CREDd\0"sv};
  Message m5{"CRED\\\0pw"sv};

  REQUIRE(std::holds_alternative<Invalid>(m1));
  REQUIRE(std::holds_alternative<Invalid>(m2));
  REQUIRE(std::holds_alternative<Invalid>(m3));
  REQUIRE(std::holds_alternative<Invalid>(m4));
  REQUIRE(std::holds_alternative<Invalid>(m5));
}

TEST_CASE("CRED allows empty domain and password", "[message]") {
  Message m1{"CRED\\user\0"sv};
  Message m2{"CREDdomain\\u\0"sv};
  Message m3{"CRED\\u\0pass"sv};

  REQUIRE(std::holds_alternative<Credential>(m1));
  auto &c1 = std::get<Credential>(m1);
  REQUIRE(c1.domain.empty());
  REQUIRE(c1.username == "user"sv);
  REQUIRE(c1.password.empty());

  REQUIRE(std::holds_alternative<Credential>(m2));
  auto &c2 = std::get<Credential>(m2);
  REQUIRE(c2.domain == "domain"sv);
  REQUIRE(c2.username == "u"sv);
  REQUIRE(c2.password.empty());

  REQUIRE(std::holds_alternative<Credential>(m3));
  auto &c3 = std::get<Credential>(m3);
  REQUIRE(c3.domain.empty());
  REQUIRE(c3.username == "u"sv);
  REQUIRE(c3.password == "pass"sv);
}

TEST_CASE("BLES with wrong size handle deserializes to invalid", "[message]") {
  Message m0{"BLES"sv};
  Message m1{"BLES0123"sv};
  Message m2{"BLES01234567"sv};

  REQUIRE(std::holds_alternative<Invalid>(m0));

  Message *good, *bad;
  void *p;
  if constexpr (sizeof(void *) == 4) {
    good = &m1;
    bad = &m2;
    p = *reinterpret_cast<void *const *>("0123");
  } else if constexpr (sizeof(void *) == 8) {
    good = &m2;
    bad = &m1;
    p = *reinterpret_cast<void *const *>("012345678");
  } else {
    FAIL("seriously, what type of device do you have??");
  }

  REQUIRE(std::holds_alternative<Bless>(*good));
  REQUIRE(std::holds_alternative<Invalid>(*bad));
  REQUIRE(std::get<Bless>(*good).hRemoteProcess == p);
}

TEST_CASE("Message round trip", "[message]") {
  Message m;
  std::string buf;
  buf.reserve(64);

  {
    buf.clear();
    m.serialize(buf);
    REQUIRE(buf == "INVM"sv);
    Message m2{buf};
    REQUIRE(std::holds_alternative<Invalid>(m2));
  }

  {
    buf.clear();
    (m = Success{}).serialize(buf);
    REQUIRE(buf == "SUCC"sv);
    Message m2{buf};
    REQUIRE(std::holds_alternative<Success>(m2));
  }

  {
    buf.clear();
    (m = Failure{}).serialize(buf);
    REQUIRE(buf == "FAIL"sv);
    Message m2{buf};
    REQUIRE(std::holds_alternative<Failure>(m2));
    auto &fail = std::get<Failure>(m2);
    REQUIRE(fail.reason.empty());
  }

  {
    buf.clear();
    (m = Failure{"reason"}).serialize(buf);
    REQUIRE(buf == "FAILreason"sv);
    Message m2{buf};
    REQUIRE(std::holds_alternative<Failure>(m2));
    auto &fail = std::get<Failure>(m2);
    REQUIRE(fail.reason == "reason"sv);
  }

  {
    buf.clear();
    (m = InternalError{}).serialize(buf);
    REQUIRE(buf == "INTE"sv);
    Message m2{buf};
    REQUIRE(std::holds_alternative<InternalError>(m2));
  }

  {
    buf.clear();
    (m = AccessDenied{}).serialize(buf);
    REQUIRE(buf == "DENY"sv);
    Message m2{buf};
    REQUIRE(std::holds_alternative<AccessDenied>(m2));
  }

  {
    buf.clear();
    (m = QuerySession{"domain", "username"}).serialize(buf);
    REQUIRE(buf == "QSESdomain\\username"sv);
    Message m2{buf};
    REQUIRE(std::holds_alternative<QuerySession>(m2));
    auto &query = std::get<QuerySession>(m2);
    REQUIRE(query.domain == "domain"sv);
    REQUIRE(query.username == "username"sv);
  }

  {
    buf.clear();
    (m = Credential{"domain", "username", "password"}).serialize(buf);
    REQUIRE(buf == "CREDdomain\\username\0password"sv);
    Message m2{buf};
    REQUIRE(std::holds_alternative<Credential>(m2));
    auto &cred = std::get<Credential>(m2);
    REQUIRE(cred.domain == "domain"sv);
    REQUIRE(cred.username == "username"sv);
    REQUIRE(cred.password == "password"sv);
  }

  {
    buf.clear();
    (m = Bless{&m}).serialize(buf);
    REQUIRE(buf.substr(0, 4) == "BLES"sv);
    std::string_view handlestr{reinterpret_cast<const char *>(&m),
                               sizeof(void *)};
    REQUIRE(buf.substr(4) == handlestr);
    Message m2{buf};
    REQUIRE(std::holds_alternative<Bless>(m2));
    auto &bless = std::get<Bless>(m2);
    REQUIRE(bless.hRemoteProcess == &m);
  }
}
