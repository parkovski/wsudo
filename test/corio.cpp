#include "wsudo/wsudo.h"
#include "wsudo/corio.h"

#include <catch2/catch_all.hpp>

#include <sstream>

using namespace wsudo;

wscoro::Task<> dothestuff(CorIO &corio, CorIO::File &file, std::string &buf) {
  REQUIRE(co_await file.read(buf));
  corio.postQuitMessage(0);
}

TEST_CASE("Coroutine IO", "[corio]") {
  const wchar_t *const gpl3_path = L"..\\LICENSE";
  constexpr size_t gpl3_size_lf = 35149;
  constexpr size_t gpl3_size_crlf = 35823;

  CorIO corio;
  corio.run(1);

  auto file = corio.openForReading(gpl3_path, FILE_FLAG_SEQUENTIAL_SCAN);

  std::string buf;
  auto task = ([&] () -> wscoro::Task<> {
    co_await file.read(buf);
    corio.postQuitMessage(0);
  })();
  REQUIRE(!task.await_ready());
  task.resume();

  corio.wait();

  REQUIRE(task.await_ready());
  auto size = buf.size();
  if (size == gpl3_size_lf || size == gpl3_size_crlf) {
    SUCCEED("Coroutine IO read the correct number of bytes.");
  } else {
    std::stringstream s;
    s << "GPLv3 should be " << gpl3_size_lf << " (LF) or " << gpl3_size_crlf
      << " (CRLF) bytes; read " << size << ".";
    FAIL(s.str());
  }
}
