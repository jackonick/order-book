#include "order_book.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>

bool OrderBook::canFill(Order incoming, bool is_buy) { // true means buy
  bool canFillNotKill = false;
  uint64_t volume = 0;

  if (is_buy) {
    for (auto &[price, orders] : asks) {
      if (price <= incoming.price) {
        for (auto &order : orders) {
          volume += order.size;
        }
      }
    }
  }

  else {
    for (auto &[price, orders] : bids) {
      if (price >= incoming.price) {
        for (auto &order : orders) {
          volume += order.size;
        }
      }
    }
  }

  return volume >= incoming.size;
}

Outcome OrderBook::add_order(Order incoming) {
  Outcome outcome;
  uint64_t starting_size = incoming.size;

  if (incoming.side == Side::BUY) {
    if (incoming.type == Type::BOC) {
      if (!asks.empty() && incoming.price >= asks.begin()->first) {
        std::cerr << "BOC rejected\n";

        outcome.reason = Reason::BOC_CANCELLED;
        outcome.quantity_rested = 0;
        outcome.quantity_filled = 0;
        outcome.assigned_id = incoming.id;

        return outcome;
      }
    }

    if (incoming.type == Type::FOK && !canFill(incoming, true)) {

      outcome.reason = Reason::FOK_NOT_FILLED;
      outcome.quantity_rested = 0;
      outcome.quantity_filled = 0;
      outcome.assigned_id = incoming.id;

      return outcome;
    }

    while (incoming.size > 0 && !asks.empty() &&
           (incoming.type == Type::MARKET ||
            asks.begin()->first <= incoming.price)) {
      Order &resting = asks.begin()->second.front();
      uint64_t trade_size = std::min(incoming.size, resting.size);

      incoming.size -= trade_size;
      resting.size -= trade_size;

      Trade t1;
      t1.resting_id = resting.id;
      t1.resting_price = resting.price;
      t1.trade_size = trade_size;
      t1.incoming_id = incoming.id;
      Trades.push_back(t1);

      eventTrade e;
      e.trade_price = resting.price;
      e.trade_size = trade_size;
      e.resting_id = resting.id;
      e.incoming_id = incoming.id;
      e.seq_num = seq_num++;
      events.push_back(e);

      if (resting.size == 0) {
        if (resting.type == Type::iceberg && resting.reserve > 0) {
          Order refill = resting;
          uint64_t slice = std::min(refill.display_size, refill.reserve);

          auto now = std::chrono::steady_clock::now();
          uint64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            now.time_since_epoch())
                            .count();

          refill.size = slice;
          refill.reserve -= slice;
          refill.timestamp = ts;

          asks.begin()->second.pop_front();
          asks[refill.price].push_back(refill);

          eventAdd a;
          a.side = refill.side;
          a.price = refill.price;
          a.timestamp = ts;
          a.id = refill.id;
          a.size = refill.size;
          a.seq_num = seq_num++;
          events.push_back(a);

        }

        else {
          asks.begin()->second.pop_front();
          idIndex.erase(resting.id);
          if (asks.begin()->second.empty()) {
            asks.erase(asks.begin());
          }
        }
      }
    }

    outcome.quantity_filled = starting_size - incoming.size;
    outcome.quantity_rested = 0;
    outcome.assigned_id = incoming.id;
    outcome.reason = Reason::ACCEPTED;

    if (incoming.type != Type::IOC && incoming.type != Type::MARKET) {
      if (incoming.size > 0) {
        bids[incoming.price].push_back(incoming);
        idIndex[incoming.id] = {incoming.price, incoming.side};

        eventAdd a;
        a.side = incoming.side;
        a.price = incoming.price;
        a.timestamp = incoming.timestamp;
        a.id = incoming.id;
        a.size = incoming.size;
        a.seq_num = seq_num++;
        events.push_back(a);

        outcome.quantity_rested = incoming.size;
      }
    }
  }

  else if (incoming.side == Side::SELL) {
    if (incoming.type == Type::BOC) {
      if (!bids.empty() && incoming.price <= bids.begin()->first) {
        std::cerr << "BOC rejected\n";

        outcome.reason = Reason::BOC_CANCELLED;
        outcome.quantity_rested = 0;
        outcome.quantity_filled = 0;
        outcome.assigned_id = incoming.id;

        return outcome;
      }
    }

    if (incoming.type == Type::FOK && !canFill(incoming, false)) {
      outcome.reason = Reason::FOK_NOT_FILLED;
      outcome.quantity_rested = 0;
      outcome.quantity_filled = 0;
      outcome.assigned_id = incoming.id;

      return outcome;
    }

    while (incoming.size > 0 && !bids.empty() &&
           (incoming.type == Type::MARKET ||
            bids.begin()->first >= incoming.price)) {
      Order &resting = bids.begin()->second.front();
      uint64_t trade_size = std::min(incoming.size, resting.size);

      incoming.size -= trade_size;
      resting.size -= trade_size;

      Trade t1;
      t1.resting_id = resting.id;
      t1.resting_price = resting.price;
      t1.trade_size = trade_size;
      t1.incoming_id = incoming.id;
      Trades.push_back(t1);

      if (resting.size == 0) {
        if (resting.type == Type::iceberg && resting.reserve > 0) {
          Order refill = resting;
          uint64_t slice = std::min(refill.display_size, refill.reserve);

          refill.size = slice;
          refill.reserve -= slice;

          bids.begin()->second.pop_front();
          bids[refill.price].push_back(refill);
        }

        else {
          bids.begin()->second.pop_front();
          idIndex.erase(resting.id);
          if (bids.begin()->second.empty()) {
            bids.erase(bids.begin());
          }
        }
      }
    }

    outcome.quantity_filled = starting_size - incoming.size;
    outcome.quantity_rested = 0;
    outcome.assigned_id = incoming.id;
    outcome.reason = Reason::ACCEPTED;

    if (incoming.type != Type::IOC && incoming.type != Type::MARKET) {
      if (incoming.size > 0) {
        asks[incoming.price].push_back(incoming);
        idIndex[incoming.id] = {incoming.price, incoming.side};

        outcome.quantity_rested = incoming.size;
      }
    }
  }

  return outcome;
}

Outcome2 OrderBook::cancel_id(uint64_t id) {
  Outcome2 outcome;
  outcome.reason = Reason::CANCEL_ACCEPTED;
  outcome.assigned_id = id;

  auto it = idIndex.find(id);
  if (it == idIndex.end()) {
    outcome.reason = Reason::CANCEL_FAILED;
    return outcome;
  }

  location loc = it->second;

  if (loc.side == Side::BUY) {
    auto &deque = bids[loc.price];
    for (auto oit = deque.begin(); oit != deque.end(); ++oit) {
      if (oit->id == id) {
        deque.erase(oit);
        idIndex.erase(id);

        outcome.reason = Reason::CANCEL_ACCEPTED;
        break;
      }
    }

    if (bids[loc.price].empty()) {
      bids.erase(loc.price);
    }
  }

  else {
    auto &deque = asks[loc.price];
    for (auto oit = deque.begin(); oit != deque.end(); ++oit) {
      if (oit->id == id) {
        deque.erase(oit);
        idIndex.erase(id);

        outcome.reason = Reason::CANCEL_ACCEPTED;
        break;
      }
    }

    if (asks[loc.price].empty()) {
      asks.erase(loc.price);
    }
  }

  return outcome;
}

Outcome2 OrderBook::modify_order(uint64_t id,
                                 uint64_t new_size) { // modify order size by id
  Outcome2 outcome;
  outcome.reason = Reason::MODIFY_ACCEPTED;
  outcome.assigned_id = id;

  if (new_size == 0) {
    cancel_id(id);

    outcome.reason = Reason::CANCEL_ACCEPTED;
    return outcome;
  }

  Order *found = id_searcher(id);
  if (found == nullptr) {
    std::cerr << "id searcher returned null\n";

    outcome.reason = Reason::MODIFY_FAILED;
    return outcome;
  }

  found->size = new_size;
  return outcome;
}

Outcome2 OrderBook::modify_price(uint64_t id, uint64_t new_price) {
  Order saved;
  Outcome2 outcome;
  outcome.reason = Reason::MODIFY_ACCEPTED;
  outcome.assigned_id = id;

  if (new_price == 0) {
    cancel_id(id);

    outcome.reason = Reason::CANCEL_ACCEPTED;
    return outcome;
  }

  Order *found = id_searcher(id);
  if (found == nullptr) {
    std::cerr << "id searcher returned null\n";

    outcome.reason = Reason::MODIFY_FAILED;
    return outcome;
  }

  saved = *found;
  cancel_id(id);
  saved.price = new_price;
  add_order(saved);

  return outcome;
}

void OrderBook::print() const {
  std::cout << "---ASKS---\n";
  for (const auto &[price, orders] : asks) {
    std::cout << " Price= " << price << "\n";
    for (const auto &order : orders) {
      std::cout << " Order_ID=" << order.id << " Order_Size=" << order.size
                << "\n";
    }
  }

  std::cout << "---BIDS---\n";
  for (const auto &[price, orders] : bids) {
    std::cout << " Price= " << price << "\n";
    for (const auto &order : orders) {
      std::cout << " Order_ID=" << order.id << " Order_Size=" << order.size
                << "\n";
    }
  }
}

void OrderBook::printTrade() const {
  std::cout << "---TRADES---\n";
  std::fstream file("trades.csv", std::ios::out);

  if (!file.is_open()) {
    std::cerr << "Error: couldnt create or open file. \n";
  }

  file << "resting id, trade size, incoming id, resting price\n"; // header line

  for (const auto &t : Trades) {
    file << t.resting_id << ",";
    file << t.trade_size << ",";
    file << t.incoming_id << ",";
    file << t.resting_price << "\n";
  }
  file.close();
}

void OrderBook::printBbo() const {
  if (!bids.empty()) {
    std::cout << " best bid: " << bids.begin()->first;
  } else {
    std::cout << "| no bids. \n";
  }

  if (!asks.empty()) {
    std::cout << "| best ask: " << asks.begin()->first;
  } else {
    std::cout << "| no asks. \n";
  }

  if (!bids.empty() && !asks.empty()) {
    uint64_t spread = asks.begin()->first - bids.begin()->first;
    std::cout << "spread: " << spread << "\n";
  }
}

void OrderBook::printDepth(int N) const {
  int count = 0;

  std::cout << "---BIDS---\n";
  for (auto &[price, orders] : bids) {
    if (count >= N) {
      break;
    }
    uint64_t total = 0;
    for (auto &order : orders) {
      total += order.size;
    }
    std::cout << "price: " << price << "| total: " << total << "\n";
    count++;
  }

  count = 0;
  std::cout << "---ASKS---\n";
  for (auto &[price, orders] : asks) {
    if (count >= N) {
      break;
    }
    uint64_t total = 0;
    for (auto &order : orders) {
      total += order.size;
    }
    std::cout << "price: " << price << "| total: " << total << "\n";
    count++;
  }
}

OrderBook::OrderBook() { Trades.reserve(10000); }

std::size_t OrderBook::bid_levels() const { return bids.size(); }

std::size_t OrderBook::ask_levels() const { return asks.size(); }

std::size_t OrderBook::trade_count() const { return Trades.size(); }

uint64_t OrderBook::id_getter() const { return Trades.back().resting_id; }

uint64_t OrderBook::price_getter(uint64_t id) {
  Order *found = id_searcher(id);
  if (found == nullptr) {
    std::cerr << "id searcher returned null\n";
    return 0;
  }

  return found->price;
}

Order *OrderBook::id_searcher(uint64_t id) {
  for (auto &[price, orders] : asks) {
    for (auto it = orders.begin(); it != orders.end(); ++it) {
      if (it->id == id) {
        return &*it;
      }
    }
  }

  for (auto &[price, orders] : bids) {
    for (auto it = orders.begin(); it != orders.end(); ++it) {
      if (it->id == id) {
        return &*it;
      }
    }
  }
  return nullptr;
}

uint64_t OrderBook::size_getter(uint64_t id) {
  Order *found = id_searcher(id);
  if (found == nullptr) {
    std::cerr << "id searcher returned null\n";
    return 0;
  }

  return found->size;
}
