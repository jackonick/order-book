#pragma once
#include <cstdint>
#include <map>
#include <deque>
#include <vector>


enum class Side {
	BUY,
	SELL
};

struct Order {
	Side side;
	uint64_t price;
	uint64_t timestamp;
	uint64_t id;
	uint64_t size;
};

struct Trade {
	uint64_t resting_id;
	uint64_t resting_price;
	uint64_t trade_size;
	uint64_t incoming_id;
};

class OrderBook {
public:
	void add_order(Order incoming);
	void cancel_id(uint64_t resting);
	void modify_order(uint64_t id, uint64_t size);
	void print() const;
	void printTrade() const;

	std::size_t bid_levels() const;
	std::size_t ask_levels() const;
	std::size_t trade_count() const;
	uint64_t id_getter() const;
	uint64_t size_getter(uint64_t id) const;


private:
	std::map<uint64_t, std::deque<Order>, std::greater<uint64_t>> bids;
	std::map<uint64_t, std::deque<Order>> asks;
	std::vector<Trade> Trades;
};