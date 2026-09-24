#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <unordered_map>
#include <variant>
#include <vector>

enum class Side { BUY, SELL };

enum class Type {
  GTC,
  FOK,
  MARKET,
  IOC,
  BOC, // book or cancel
  iceberg
};

enum class Reason {
  FOK_NOT_FILLED,
  IOC_NOT_FILLED,
  ACCEPTED,
  BOC_CANCELLED,
  MODIFY_FAILED,
  MODIFY_ACCEPTED,
  CANCEL_FAILED,
  CANCEL_ACCEPTED,
  NOTHING_TO_MODIFY
};

struct Outcome {
  Reason reason;
  uint64_t quantity_rested = 0;
  uint64_t quantity_filled = 0;
  uint64_t assigned_id = 0;
};

struct Outcome2 {
  Reason reason;
  uint64_t assigned_id = 0;
};

struct Order {
  Side side;
  Type type;
  uint64_t price;
  uint64_t timestamp;
  uint64_t id;
  uint64_t size;
  uint64_t reserve;
  uint64_t display_size;
};

struct eventAdd {
  Side side;
  uint64_t price;
  uint64_t timestamp;
  uint64_t id;
  uint64_t size;
  uint64_t seq_num;
};

struct eventCancel {
  uint64_t price;
  uint64_t id;
  uint64_t seq_num;
};

struct eventModifyPrice {
  uint64_t price;
  uint64_t new_price;
  uint64_t id;
  uint64_t seq_num;
};

struct eventModifySize {
  uint64_t size;
  uint64_t new_size;
  uint64_t price;
  uint64_t id;
  uint64_t seq_num;
};

struct eventTrade {
  uint64_t trade_price;
  uint64_t trade_size;
  uint64_t resting_id;
  uint64_t incoming_id;
  uint64_t seq_num;
};

using Event = std::variant<eventAdd, eventCancel, eventModifyPrice,
                           eventModifySize, eventTrade>;

struct Trade {
  uint64_t resting_id;
  uint64_t resting_price;
  uint64_t trade_size;
  uint64_t incoming_id;
};

struct location {
  uint64_t price;
  Side side;
};

class OrderBook {
public:
  OrderBook();
  Outcome add_order(Order incoming);
  Outcome2 cancel_id(uint64_t resting);
  Outcome2 modify_order(uint64_t id, uint64_t size);
  Outcome2 modify_price(uint64_t id, uint64_t price);
  void print() const;
  void printTrade() const;
  void printBbo() const;
  void printDepth(int N) const;

  std::size_t bid_levels() const;
  std::size_t ask_levels() const;
  std::size_t trade_count() const;
  uint64_t id_getter() const;
  uint64_t size_getter(uint64_t id);
  uint64_t price_getter(uint64_t id);
  Order *id_searcher(uint64_t id);
  bool canFill(Order incoming, bool is_buy);

private:
  std::map<uint64_t, std::deque<Order>, std::greater<uint64_t>> bids;
  std::map<uint64_t, std::deque<Order>> asks;
  std::vector<Trade> Trades;
  std::unordered_map<uint64_t, location> idIndex;
  std::vector<Event> events;
  uint64_t seq_num = 0;
};
