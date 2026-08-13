#include "northstar64/uart.hpp"
#include "test_support.hpp"

#include <sstream>
#include <variant>

using namespace northstar64;

TEST_CASE("UART transmits bytes and reports an empty transmitter") {
  std::ostringstream output;
  Uart16550 uart(Uart16550::kDefaultBase, &output);
  CHECK(!uart.write(0, 1, 'N'));
  CHECK(!uart.write(0, 1, 'S'));
  CHECK_EQ(uart.transmitted(), std::string("NS"));
  CHECK_EQ(output.str(), std::string("NS"));
  CHECK_EQ(std::get<std::uint64_t>(uart.read(5, 1)), std::uint64_t{0x60});
}

TEST_CASE("UART rejects wide accesses") {
  Uart16550 uart;
  CHECK(std::holds_alternative<DeviceError>(uart.read(0, 4)));
  CHECK(uart.write(0, 8, 'x').has_value());
}
