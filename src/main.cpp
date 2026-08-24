#include "order_book.h"
#include <iostream>

int main()
{
	OrderBook book;

	Order o1;
	o1.id = 1;
	o1.side = Side::SELL;
	o1.price = 100;
	o1.size = 10;
	o1.timestamp = 0;
    book.add_order(o1);

	Order o2;
	o2.id = 2;
	o2.side = Side::SELL;
	o2.price = 101;
	o2.size = 5;
	o2.timestamp = 1;
    book.add_order(o2);

	std::cout << "\n--- After two SELLs ---\n";
    book.print();

	Order o3;
	o3.id = 3;
	o3.side = Side::BUY;
	o3.price = 100;
	o3.size = 7;
	o3.timestamp = 2;
    book.add_order(o3);

	std::cout << "\n--- After BUY id=3 (should partial-fill id=1) ---\n";
    book.print(); 

	Order o4;
	o4.id = 4;
	o4.side = Side::BUY;
	o4.price = 99;
	o4.size = 5;
	o4.timestamp = 3;
    book.add_order(o4);

	std::cout << "\n--- After BUY id=4 (rests on bids) ---\n";
    book.print();
    
	Order o5;
	o5.id = 5;
	o5.side = Side::BUY;
	o5.price = 102;
	o5.size = 20;
	o5.timestamp = 4;
    book.add_order(o5);

	std::cout << "\n--- After BUY id=5 (walks both ask levels) ---\n";
    
    book.printTrade();
	book.printBbo();

	return 0;
}