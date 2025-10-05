#pragma once

enum class MouseCode
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

    // 别名定义（保持向后兼容）
    Button1 = ButtonLeft,
    Button2 = ButtonRight,
    Button3 = ButtonMiddle,

    // 特殊值
    ButtonLast = Button8
};