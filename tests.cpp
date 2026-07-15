#include <gtest/gtest.h>
#include "order_book.h"

TEST(Suite, test){
    EXPECT_EQ(1, 1);
    EXPECT_TRUE(true);
}


TEST(RestsWhenNoMatch, AddOrder){
    OrderBook book;
    Order o1;
    o1.id = 5;
	o1.side = Side::BUY;
	o1.price = 102;
	o1.size = 20;
	o1.timestamp = 4;
    book.add_order(o1);

    EXPECT_EQ(book.bid_levels(), 1);
}

TEST(Matching, CrossingProducesTrade){
    OrderBook book;
    book.print();
    Order o1;
    o1.id = 5;
	o1.side = Side::BUY;
	o1.price = 102;
	o1.size = 20;
	o1.timestamp = 4;

    Order o2;
    o2.id = 6;
    o2.side = Side::SELL;
    o2.price = 102;
    o2.size = 50;
    o2.timestamp = 5;
    book.add_order(o2);

    EXPECT_EQ(book.trade_count(), 0);

    book.add_order(o1);

    EXPECT_EQ(book.trade_count(), 1);

    book.print();
}

TEST(Crossing, SomeNameForTest){
    OrderBook book;
    
    Order o1;
    o1.id = 1;
	o1.side = Side::BUY;
	o1.price = 100;
	o1.size = 10;
	o1.timestamp = 4;
    book.add_order(o1);

    Order o2;
    o2.id = 2;
    o2.side = Side::BUY;
    o2.price = 100;
    o2.size = 10;
    o2.timestamp = 5;
    book.add_order(o2);

    Order o3;
    o3.id = 3;
    o3.side = Side::SELL;
    o3.price = 100;
    o3.size = 10;
    o3.timestamp = 6;
    book.add_order(o3);

    EXPECT_EQ(book.trade_count(), 1);
    EXPECT_EQ(book.id_getter(), 1);
}

TEST(Canceling, CancelCancels){
    OrderBook book;
    
    Order o1;
    o1.id = 1;
	o1.side = Side::BUY;
	o1.price = 100;
	o1.size = 10;
	o1.timestamp = 4;
    book.add_order(o1);

    EXPECT_EQ(book.bid_levels(), 1);

    book.cancel_id(1);

    EXPECT_EQ(book.bid_levels(), 0);
}

TEST(Canceling, AsksAlsoCancel){
    OrderBook book;

    Order o3;
    o3.id = 3;
    o3.side = Side::SELL;
    o3.price = 100;
    o3.size = 10;
    o3.timestamp = 6;
    book.add_order(o3);

    EXPECT_EQ(book.ask_levels(), 1);

    book.cancel_id(3);

    EXPECT_EQ(book.ask_levels(), 0);
}

TEST(Modifying, BasicModify){
    OrderBook book;
    
    Order o1;
    o1.id = 1;
    o1.side = Side::BUY;
	o1.price = 100;
	o1.size = 10;
	o1.timestamp = 4;
    book.add_order(o1);
    EXPECT_EQ(book.size_getter(1), 10);

    book.modify_order(1, 9);
    EXPECT_EQ(book.size_getter(1), 9);

    book.modify_order(1, 0);
    EXPECT_EQ(book.bid_levels(), 0);

}