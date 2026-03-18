#ifndef __ARDUINO_CORE_NODE_H__
#define __ARDUINO_CORE_NODE_H__
#include "rcore/c_target.h"
#ifdef USE_PRAGMA_ONCE
#    pragma once
#endif

namespace ncore
{
    struct state_t;
    struct state_task_t;

    namespace nnode
    {
        void initialize(state_t* state, state_task_t* task_state);
        void send_sensor_data(state_t* state, const byte* data, const s32 size);

    }  // namespace nnode
}  // namespace ncore

#endif  // __ARDUINO_CORE_NODE_H__
