#ifndef MATCHING_ENGINE_H
#define MATCHING_ENGINE_H

#include <list>


#include "order.h"
#include "order-book.h"




class matching_engine {
    private:
        OrderBook book;

    public:
        void submit_order(Order order);

};


#endif