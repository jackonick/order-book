#include "event_convert.h"
#include <stdexcept>

exchange::MarketDataEvent to_proto(const Event &e) {
  exchange::MarketDataEvent out;

  if (const auto *a = std::get_if<eventAdd>(&e)) {
    out.set_seq(a->seq_num);
    exchange::EventAdd *add = out.mutable_add();
    add->set_side(a->side == Side::BUY ? exchange::SIDE_BUY
                                       : exchange::SIDE_SELL);
    add->set_price(a->price);
    add->set_timestamp(a->timestamp);
    add->set_id(a->id);
    add->set_size(a->size);
  }

  else if (const auto *c = std::get_if<eventCancel>(&e)) {
    out.set_seq(c->seq_num);
    exchange::EventCancel *cancel = out.mutable_cancel();
    cancel->set_price(c->price);
    cancel->set_id(c->id);
  }

  else if (const auto *mp = std::get_if<eventModifyPrice>(&e)) {
    out.set_seq(mp->seq_num);
    exchange::EventModifyPrice *modify = out.mutable_modify_price();
    modify->set_price(mp->price);
    modify->set_new_price(mp->new_price);
    modify->set_id(mp->id);
  }

  else if (const auto *ms = std::get_if<eventModifySize>(&e)) {
    out.set_seq(ms->seq_num);
    exchange::EventModifySize *modify = out.mutable_modify_size();
    modify->set_size(ms->size);
    modify->set_new_size(ms->new_size);
    modify->set_price(ms->price);
    modify->set_id(ms->id);
  }

  else if (const auto *t = std::get_if<eventTrade>(&e)) {
    out.set_seq(t->seq_num);
    exchange::EventTrade *trade = out.mutable_trade();
    trade->set_trade_price(t->trade_price);
    trade->set_trade_size(t->trade_size);
    trade->set_resting_id(t->resting_id);
    trade->set_incoming_id(t->incoming_id);
  }

  return out;
}

Event from_proto(const exchange::MarketDataEvent &m) {
  switch (m.event_case()) {
  case exchange::MarketDataEvent::kAdd: {
    eventAdd a;
    a.side = m.add().side() == exchange::SIDE_BUY ? Side::BUY : Side::SELL;
    a.price = m.add().price();
    a.timestamp = m.add().timestamp();
    a.id = m.add().id();
    a.size = m.add().size();
    a.seq_num = m.seq();
    return a;
  }

  case exchange::MarketDataEvent::kCancel: {
    eventCancel c;
    c.price = m.cancel().price();
    c.id = m.cancel().id();
    c.seq_num = m.seq();
    return c;
  }

  case exchange::MarketDataEvent::kModifyPrice: {
    eventModifyPrice mp;
    mp.price = m.modify_price().price();
    mp.new_price = m.modify_price().new_price();
    mp.id = m.modify_price().id();
    mp.seq_num = m.seq();
    return mp;
  }

  case exchange::MarketDataEvent::kModifySize: {
    eventModifySize ms;
    ms.size = m.modify_size().size();
    ms.new_size = m.modify_size().new_size();
    ms.price = m.modify_size().price();
    ms.id = m.modify_size().id();
    ms.seq_num = m.seq();
    return ms;
  }

  case exchange::MarketDataEvent::kTrade: {
    eventTrade t;
    t.trade_price = m.trade().trade_price();
    t.trade_size = m.trade().trade_size();
    t.resting_id = m.trade().resting_id();
    t.incoming_id = m.trade().incoming_id();
    t.seq_num = m.seq();
    return t;
  }

  case exchange::MarketDataEvent::EVENT_NOT_SET:
    break;
  }

  throw std::runtime_error("from_proto: MarketDataEvent has no event set");
}
