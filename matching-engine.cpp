#include "matching-engine.h"

void matching_engine::submit_order(Order &order) { book.submit_order(order); }
void matching_engine::cancel_order(OrderId id) { book.cancel_order(id); }
