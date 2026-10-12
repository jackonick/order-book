#include "exchange_service.h"
#include "event_convert.h"
#include <algorithm>
#include <chrono>
#include <thread>

grpc::Status ExchangeService::SubmitOrder(grpc::ServerContext *context,
                                          const exchange::NewOrderRequest *request,
                                          exchange::OrderResponse *response) {
  std::lock_guard<std::mutex> lock(mtx);
  counter++;

  Order o;
  o.price = request->price();
  o.size = request->size();
  o.reserve = request->reserve();
  o.display_size = request->display_size();

  o.side = static_cast<Side>(request->side());
  o.type = static_cast<Type>(request->order_type());

  auto now = std::chrono::steady_clock::now();
  uint64_t ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    now.time_since_epoch())
                    .count();

  o.id = counter;
  o.timestamp = ts;

  Outcome outcome = book.add_order(o);

  response->set_assigned_id(outcome.assigned_id);
  response->set_accepted(outcome.reason == Reason::ACCEPTED);
  response->set_reason(static_cast<uint32_t>(outcome.reason));
  response->set_filled_size(outcome.quantity_filled);

  broadcast_locked();

  return grpc::Status::OK;
}

grpc::Status ExchangeService::SubmitCancel(grpc::ServerContext *context,
                                           const exchange::NewCancelRequest *request,
                                           exchange::CancelResponse *response) {
  std::lock_guard<std::mutex> lock(mtx);

  Outcome2 outcome = book.cancel_id(request->id());

  response->set_reason(static_cast<uint32_t>(outcome.reason));

  broadcast_locked();

  return grpc::Status::OK;
}

grpc::Status ExchangeService::SubmitModify(grpc::ServerContext *context,
                                           const exchange::NewModifyOrder *request,
                                           exchange::ModifyResponse *response) {
  std::lock_guard<std::mutex> lock(mtx);

  if (!request->has_price() && !request->has_size()) {
    response->set_reason(static_cast<uint32_t>(Reason::NOTHING_TO_MODIFY));
    response->set_accepted(false);
    response->set_id(request->id());
    return grpc::Status::OK;
  }

  Outcome2 outcome;
  outcome.reason = Reason::MODIFY_ACCEPTED;
  outcome.assigned_id = request->id();

  if (request->has_price()) {
    outcome = book.modify_price(request->id(), request->price());
  }

  if (request->has_size() && outcome.reason == Reason::MODIFY_ACCEPTED) {
    outcome = book.modify_order(request->id(), request->size());
  }

  response->set_reason(static_cast<uint32_t>(outcome.reason));
  response->set_accepted(outcome.reason == Reason::MODIFY_ACCEPTED ||
                         outcome.reason == Reason::CANCEL_ACCEPTED);
  response->set_id(outcome.assigned_id);

  broadcast_locked();

  return grpc::Status::OK;
}

grpc::Status ExchangeService::Subscribe(
    grpc::ServerContext *context, const exchange::SubscribeRequest *request,
    grpc::ServerWriter<exchange::MarketDataEvent> *writer) {
  (void)request;

  Subscriber self;
  self.writer = writer;

  {
    std::lock_guard<std::mutex> lock(mtx);

    Snapshot snap = book.snapshot();

    exchange::MarketDataEvent header;
    header.set_seq(snap.as_of);
    header.set_snapshot(true);
    if (!writer->Write(header)) {
      self.alive = false;
    }

    for (const SnapshotOrder &o : snap.orders) {
      if (!self.alive) {
        break;
      }

      exchange::MarketDataEvent msg;
      msg.set_seq(snap.as_of);
      msg.set_snapshot(true);

      exchange::EventAdd *add = msg.mutable_add();
      add->set_side(o.side == Side::BUY ? exchange::SIDE_BUY
                                        : exchange::SIDE_SELL);
      add->set_price(o.price);
      add->set_timestamp(o.timestamp);
      add->set_id(o.id);
      add->set_size(o.size);

      if (!writer->Write(msg)) {
        self.alive = false;
      }
    }

    if (self.alive) {
      subs.push_back(&self);
    }
  }

  while (!context->IsCancelled()) {
    {
      std::lock_guard<std::mutex> lock(mtx);
      if (!self.alive) {
        break;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  {
    std::lock_guard<std::mutex> lock(mtx);
    subs.erase(std::remove(subs.begin(), subs.end(), &self), subs.end());
  }

  return grpc::Status::OK;
}

void ExchangeService::broadcast_locked() {
  const std::vector<Event> &events = book.get_events();

  for (std::size_t i = published; i < events.size(); ++i) {
    exchange::MarketDataEvent msg = to_proto(events[i]);

    for (Subscriber *s : subs) {
      if (!s->alive) {
        continue;
      }

      if (!s->writer->Write(msg)) {
        s->alive = false;
      }
    }
  }

  published = events.size();
}

std::string ExchangeService::dump_book() {
  std::lock_guard<std::mutex> lock(mtx);
  return book.dump();
}

std::size_t ExchangeService::subscriber_count() {
  std::lock_guard<std::mutex> lock(mtx);
  return subs.size();
}
