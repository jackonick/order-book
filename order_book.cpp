#include <iostream>
#include "order_book.h"
#include <algorithm>
#include <vector>
#include <filesystem>
#include <fstream>

void OrderBook::add_order(Order incoming)
{
	if (incoming.side == Side::BUY)
	{
		while (incoming.size > 0 && !asks.empty() && asks.begin()->first <= incoming.price)
		{
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

			if (resting.size == 0)
			{
				asks.begin()->second.pop_front();
				if (asks.begin()->second.empty())
				{
					asks.erase(asks.begin());
				}
			}
		}

		if (incoming.size > 0)
		{
			bids[incoming.price].push_back(incoming);
		}
	}

	else if (incoming.side == Side::SELL)
	{
		while (incoming.size > 0 && !bids.empty() && bids.begin()->first >= incoming.price)
		{
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

			if (resting.size == 0)
			{
				bids.begin()->second.pop_front();
				if (bids.begin()->second.empty())
				{
					bids.erase(bids.begin());
				}
			}
		}

		if (incoming.size > 0)
		{
			asks[incoming.price].push_back(incoming);
		}
	}
}

void OrderBook::cancel_id(uint64_t id){
	uint64_t savedPrice = 0;
	bool found = false;
	for (auto& [price, orders] : bids){
		if (found){
			break;
		}
		for (auto it = orders.begin(); it != orders.end(); ++it){
			if (it->id == id){
				orders.erase(it);
				savedPrice = price;
				found = true;
				break;
			}
		}
	}
	if (found && bids[savedPrice].empty()){
		bids.erase(savedPrice);
	}

	if (found){
		return;
	}


	for (auto& [price, orders] : asks){
		if (found){
			break;
		}
		for (auto it = orders.begin(); it != orders.end(); ++it){
			if (it->id == id){
				orders.erase(it);
				savedPrice = price;
				found = true;
				break;
			}
		}
	}
	if (found && asks[savedPrice].empty()){
		asks.erase(savedPrice);
	}
}

void OrderBook::modify_order(uint64_t id, uint64_t new_size){
	if (new_size == 0){
		cancel_id(id);
		return;
	}

	for (auto& [price, orders] : asks){
		for (auto it = orders.begin(); it != orders.end(); ++it){
			if (it->id == id){
				it->size = new_size;
			}
		}
	}

	for (auto& [price, orders] : bids){
		for (auto it = orders.begin(); it != orders.end(); ++it){
			if (it->id == id){
				it->size = new_size;
			}
		}
	}
}



void OrderBook::print() const
{
	std::cout << "---ASKS---\n";
	for (const auto &[price, orders] : asks)
	{
		std::cout << " Price= " << price << "\n";
		for (const auto &order : orders)
		{
			std::cout << " Order_ID=" << order.id << " Order_Size=" << order.size << "\n";
		}
	}

	std::cout << "---BIDS---\n";
	for (const auto &[price, orders] : bids)
	{
		std::cout << " Price= " << price << "\n";
		for (const auto &order : orders)
		{
			std::cout << " Order_ID=" << order.id << " Order_Size=" << order.size << "\n";
		}
	}
}

void OrderBook::printTrade() const
{
	std::cout << "---TRADES---\n";
	std::fstream file("trades.csv", std::ios::out);

	if (!file.is_open())
	{
		std::cerr << "Error: couldnt create or open file. \n";
	}

	file << "resting id, trade size, incoming id, resting price\n"; // header line

	for (const auto &t : Trades)
	{
		file << t.resting_id << ",";
		file << t.trade_size << ",";
		file << t.incoming_id << ",";
		file << t.resting_price << "\n";
	}
	file.close();
}

std::size_t OrderBook::bid_levels () const{
	return bids.size();
}

std::size_t OrderBook::ask_levels () const{
	return asks.size();
}

std::size_t OrderBook::trade_count () const {
	return Trades.size();
}

uint64_t OrderBook::id_getter () const {
	return Trades.back().resting_id;
}

uint64_t OrderBook::size_getter (uint64_t id) const {
	for (const auto& [price, orders] : asks){
		for (auto it = orders.begin(); it != orders.end(); ++it){
			if (it->id == id){
				return it->size;
			}
		}
	}

	for (const auto& [price, orders] : bids){
		for (auto it = orders.begin(); it != orders.end(); ++it){
			if (it->id == id){
				return it->size;
			}
		}
	}

	return 0;
}