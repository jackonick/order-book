#include <gtest/gtest.h>
#include "event_convert.h"
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



TEST(Modify_price, price) {
    OrderBook book;

    Order o1;
    o1.id = 2;
    o1.side = Side::BUY;
    o1.price = 100;
    o1.size = 1;
    o1.timestamp = 4;
    book.add_order(o1);
    EXPECT_EQ(book.size_getter(2), 1);

    book.modify_price(2, 105);
    EXPECT_EQ(book.price_getter(2), 105);

    book.modify_price(2, 110);
    EXPECT_EQ(book.price_getter(2), 110);
}

TEST(market_test, market) {
    OrderBook book;

    Order o2;
    o2.id = 3;
    o2.side = Side::SELL;
    o2.price = 90;
    o2.size = 5;
    o2.timestamp = 4;
    book.add_order(o2);
    EXPECT_EQ(book.size_getter(3), (5));

    Order o1;
    o1.id = 2;
    o1.side = Side::BUY;
    o1.price = 100;
    o1.size = 1;
    o1.timestamp = 4;
    o1.type = Type::MARKET;
    book.add_order(o1);

    EXPECT_EQ(book.size_getter(3), (4));

    book.trade_count();
}


TEST(fillOrKill, fok) {
    OrderBook book;

    Order o1;
    o1.id = 3;
    o1.side = Side::SELL;
    o1.price = 90;
    o1.size = 500;
    o1.timestamp = 4;
    book.add_order(o1);

    Order o2;
    o2.id = 4;
    o2.side = Side::BUY;
    o2.price = 90;
    o2.size = 5;
    o2.timestamp = 5;
    o2.type = Type::FOK;
    book.add_order(o2);

    EXPECT_EQ(book.size_getter(3), (495));

    Order o3;
    o3.id = 5;
    o3.side = Side::BUY;
    o3.price = 92;
    o3.size = 1000;
    o3.timestamp = 6;
    o3.type = Type::FOK;
    book.add_order(o3);
    
    EXPECT_EQ(book.size_getter(3), (495));
}

TEST(restOrCancel, BOC){
    OrderBook book;

    Order o1;
    o1.id = 84;
    o1.side = Side::SELL;
    o1.price = 100;
    o1.size = 500;
    o1.timestamp = 1;
    book.add_order(o1);
    
    Order o2;
    o2.id = 86;
    o2.side = Side::BUY;
    o2.price = 100;
    o2.size = 400;
    o2.timestamp = 100041;
    o2.type = Type::BOC;
    book.add_order(o2);

    EXPECT_EQ(book.bid_levels(), 0);
    EXPECT_EQ(book.trade_count(), 0);
}

TEST(iceBergOrder, IBG) {
    OrderBook book;

    Order o1;
    o1.id = 84;
    o1.side = Side::SELL;
    o1.type = Type::iceberg;
    o1.reserve = 1000;
    o1.price = 100;
    o1.size = 500;
    o1.display_size = 500;
    o1.timestamp = 1;
    book.add_order(o1);

    Order o2;
    o2.id = 86;
    o2.side = Side::BUY;
    o2.price = 100;
    o2.size = 500;
    o2.timestamp = 100041;
    o2.type = Type::GTC;
    book.add_order(o2);
    EXPECT_EQ(book.size_getter(84), 500);

    Order o3;
    o3.id = 87;
    o3.side = Side::BUY;
    o3.price = 100;
    o3.size = 500;
    o3.timestamp = 100045;
    o3.type = Type::GTC;
    book.add_order(o3);
    EXPECT_EQ(book.size_getter(84), 500);

    Order o4;
    o4.id = 88;
    o4.side = Side::BUY;
    o4.price = 100;
    o4.size = 500;
    o4.timestamp = 100050;
    o4.type = Type::GTC;
    book.add_order(o4);
    EXPECT_EQ(book.ask_levels(), 0);

}

TEST(Replay, RebuildsIdenticalBook){
    OrderBook live;

    Order o1;
    o1.side = Side::BUY;
    o1.type = Type::GTC;
    o1.price = 100;
    o1.timestamp = 1;
    o1.id = 1;
    o1.size = 50;
    o1.reserve = 0;
    o1.display_size = 0;
    live.add_order(o1);

    Order o2;
    o2.side = Side::BUY;
    o2.type = Type::GTC;
    o2.price = 100;
    o2.timestamp = 2;
    o2.id = 2;
    o2.size = 30;
    o2.reserve = 0;
    o2.display_size = 0;
    live.add_order(o2);

    Order o3;
    o3.side = Side::BUY;
    o3.type = Type::GTC;
    o3.price = 99;
    o3.timestamp = 3;
    o3.id = 3;
    o3.size = 40;
    o3.reserve = 0;
    o3.display_size = 0;
    live.add_order(o3);

    Order o4;
    o4.side = Side::SELL;
    o4.type = Type::GTC;
    o4.price = 101;
    o4.timestamp = 4;
    o4.id = 4;
    o4.size = 25;
    o4.reserve = 0;
    o4.display_size = 0;
    live.add_order(o4);

    Order o5;
    o5.side = Side::SELL;
    o5.type = Type::iceberg;
    o5.price = 102;
    o5.timestamp = 5;
    o5.id = 5;
    o5.size = 10;
    o5.reserve = 40;
    o5.display_size = 10;
    live.add_order(o5);

    Order o6;
    o6.side = Side::SELL;
    o6.type = Type::GTC;
    o6.price = 100;
    o6.timestamp = 6;
    o6.id = 6;
    o6.size = 60;
    o6.reserve = 0;
    o6.display_size = 0;
    live.add_order(o6);

    Order o7;
    o7.side = Side::BUY;
    o7.type = Type::GTC;
    o7.price = 102;
    o7.timestamp = 7;
    o7.id = 7;
    o7.size = 10;
    o7.reserve = 0;
    o7.display_size = 0;
    live.add_order(o7);

    live.modify_order(3, 15);
    live.modify_price(4, 103);
    live.cancel_id(2);

    OrderBook replayed;
    for (const Event &e : live.get_events()) {
        replayed.apply(e);
    }

    EXPECT_EQ(live.dump(), replayed.dump());
}

TEST(EventRoundTrip, Add){
    eventAdd before;
    before.side = Side::SELL;
    before.price = 10123;
    before.timestamp = 987654321;
    before.id = 42;
    before.size = 75;
    before.seq_num = 9;

    Event back = from_proto(to_proto(Event{before}));
    const eventAdd *after = std::get_if<eventAdd>(&back);

    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->side, before.side);
    EXPECT_EQ(after->price, before.price);
    EXPECT_EQ(after->timestamp, before.timestamp);
    EXPECT_EQ(after->id, before.id);
    EXPECT_EQ(after->size, before.size);
    EXPECT_EQ(after->seq_num, before.seq_num);
}

TEST(EventRoundTrip, Cancel){
    eventCancel before;
    before.price = 9950;
    before.id = 7;
    before.seq_num = 13;

    Event back = from_proto(to_proto(Event{before}));
    const eventCancel *after = std::get_if<eventCancel>(&back);

    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->price, before.price);
    EXPECT_EQ(after->id, before.id);
    EXPECT_EQ(after->seq_num, before.seq_num);
}

TEST(EventRoundTrip, ModifyPrice){
    eventModifyPrice before;
    before.price = 10000;
    before.new_price = 10050;
    before.id = 21;
    before.seq_num = 44;

    Event back = from_proto(to_proto(Event{before}));
    const eventModifyPrice *after = std::get_if<eventModifyPrice>(&back);

    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->price, before.price);
    EXPECT_EQ(after->new_price, before.new_price);
    EXPECT_EQ(after->id, before.id);
    EXPECT_EQ(after->seq_num, before.seq_num);
}

TEST(EventRoundTrip, ModifySize){
    eventModifySize before;
    before.size = 500;
    before.new_size = 125;
    before.price = 10075;
    before.id = 33;
    before.seq_num = 58;

    Event back = from_proto(to_proto(Event{before}));
    const eventModifySize *after = std::get_if<eventModifySize>(&back);

    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->size, before.size);
    EXPECT_EQ(after->new_size, before.new_size);
    EXPECT_EQ(after->price, before.price);
    EXPECT_EQ(after->id, before.id);
    EXPECT_EQ(after->seq_num, before.seq_num);
}

TEST(EventRoundTrip, Trade){
    eventTrade before;
    before.trade_price = 10100;
    before.trade_size = 250;
    before.resting_id = 4;
    before.incoming_id = 91;
    before.seq_num = 77;

    Event back = from_proto(to_proto(Event{before}));
    const eventTrade *after = std::get_if<eventTrade>(&back);

    ASSERT_NE(after, nullptr);
    EXPECT_EQ(after->trade_price, before.trade_price);
    EXPECT_EQ(after->trade_size, before.trade_size);
    EXPECT_EQ(after->resting_id, before.resting_id);
    EXPECT_EQ(after->incoming_id, before.incoming_id);
    EXPECT_EQ(after->seq_num, before.seq_num);
}
