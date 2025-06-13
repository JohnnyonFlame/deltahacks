#pragma once

struct Ref {
    void *m_thing;
    int m_refCOunt;
    int m_size;
};

struct RValue {
    union {
        int v32;
        long long v64;
        double val;

        Ref *str;
    } rvalue;

    int flags;
    int kind;
};