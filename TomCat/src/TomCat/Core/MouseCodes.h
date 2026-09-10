#pragma once

#include <cstdint>

namespace TomCat {
    using MouseCode = uint16_t;

    namespace Mouse
    {
        enum:MouseCode
        {
            // 基本按钮
            ButtonLeft = 0,
            ButtonRight = 1,
            ButtonMiddle = 2,
            Button4 = 3,
            Button5 = 4,
            Button6 = 5,
            Button7 = 6,
            Button8 = 7,

            // 特殊值
            ButtonLast = Button8
        };
    }


}
