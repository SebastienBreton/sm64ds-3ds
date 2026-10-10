// Generated from build3ds/reference-ds/route-castle/interior-walk-route.json; buttons only.
#pragma once
struct PlayerRouteSegment { const char* checkpoint; unsigned updates; unsigned short keys; };
static const unsigned playerRouteSettle = 60;
static const PlayerRouteSegment playerRouteSegments[] = {
    {"away-door-20", 20, 0x80},
    {"away-door-40", 20, 0x80},
    {"rest-40", 30, 0x0},
    {"away-door-60", 20, 0x80},
    {"rest-60", 30, 0x0},
    {"hall-forward-60", 60, 0x40},
    {"hall-forward-rest", 30, 0x0},
    {"hall-right-40", 40, 0x10},
    {"hall-right-rest", 30, 0x0},
    {"stairs-forward-80", 80, 0x40},
    {"stairs-rest", 30, 0x0},
};
static const unsigned playerRouteLength = 390;
