#include <iostream>
#include <mysqlx/xdevapi.h>

int main() {
  try {
    mysqlx::Session session("localhost", 33060, "exchange", "devpassword");
    std::cout << "connected\n";

    mysqlx::Schema db = session.getSchema("exchange");
    mysqlx::Table events = db.getTable("events");

    events.insert("seq", "type", "ts", "payload")
        .values(1, 2, 1234567890, "hello")
        .execute();

    mysqlx::RowResult res = events.select("seq", "type", "ts").execute();
    for (mysqlx::Row row : res) {
      std::cout << row[0] << " " << row[1] << " " << row[2] << "\n";
    }

  } catch (const mysqlx::Error &err) {
    std::cout << "error: " << err << "\n";
    return 1;
  }
  return 0;
}
