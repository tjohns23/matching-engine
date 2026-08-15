#ifndef MATCHING_ENGINE_H
#define MATCHING_ENGINE_H

#include <list>

#include "order-book.h"
#include "order.h"

class matching_engine {
private:
  OrderBook book;

public:
  void submit_order(Order &order);
  void cancel_order(OrderId id);
};

#endif