// XY 1.2.1 retained command layout, authenticated from cubic insert 8d5f9970.
// No allocation, parser call or firmware address in these helpers.
#pragma once
#include <opxy/draw.hpp>
namespace opxy { namespace screen {
struct Command {
    i32 x, y, control_x, control_y, end_x, end_y;
    unsigned char type, stock25[7];
};
enum PathKind { closed_polyline, open_polyline, cubic_curve, separate_segments };
inline u32 command_count(u32 points, PathKind kind) {
    return kind == cubic_curve ? 2 : kind == separate_segments ? 2 * (points - 1) :
           points + (kind == closed_polyline ? 1 : 0);
}
inline bool command_types(const Command* commands, u32 points, PathKind kind) {
    if (!commands || points < 2 || points > 256 || (kind == cubic_curve && points != 4)) return false;
    const u32 count = command_count(points, kind);
    for (u32 i = 0; i < count; ++i) {
        const unsigned expected = kind == cubic_curve ? (i ? 4 : 1) :
            kind == separate_segments ? (i % 2 ? 2 : 1) : i == points ? 5 : i ? 2 : 1;
        if (commands[i].type != expected) return false;
    }
    return true;
}
inline void command_coordinates(Command* commands, const draw::Point* points, u32 count, PathKind kind) {
    if (kind == cubic_curve) {
        commands[0].x = points[0].x; commands[0].y = points[0].y;
        commands[1].x = points[1].x; commands[1].y = points[1].y;
        commands[1].control_x = points[2].x; commands[1].control_y = points[2].y;
        commands[1].end_x = points[3].x; commands[1].end_y = points[3].y;
    } else if (kind == separate_segments) {
        for (u32 i = 0; i + 1 < count; ++i) {
            commands[2*i].x = points[i].x; commands[2*i].y = points[i].y;
            commands[2*i+1].x = points[i+1].x; commands[2*i+1].y = points[i+1].y;
        }
    } else {
        for (u32 i = 0; i < count; ++i) {
            commands[i].x = points[i].x; commands[i].y = points[i].y;
        }
    }
}
// XY quadratic insertion 8d5f97d0 stores control at0/4 and endpoint at8/12,
// type3 at24. The preallocated vector can also hold move and line records.
inline bool quadratic_types(const Command* commands, u32 count, u32 capacity, bool initial) {
    if (!commands || count < 2 || count > capacity || capacity > 128 || (initial && count != capacity)) return false;
    for (u32 i = 0; i < count; ++i)
        if (i ? (initial ? commands[i].type != 3 : commands[i].type != 2 && commands[i].type != 3) : commands[i].type != 1)
            return false;
    return true;
}
inline bool counted_types(const Command* commands, u32 records, u32 capacity, PathKind kind, bool initial) {
    if (kind == cubic_curve || records < command_count(2, kind)) return false;
    const u32 count = kind == separate_segments ? records / 2 + 1 : records - (kind == closed_polyline ? 1 : 0);
    return count <= capacity && (!initial || count == capacity) && command_count(count, kind) == records &&
           command_types(commands, count, kind);
}
template <u32 Points>
inline u32 path_coordinates(Command* commands, const draw::CountedPath<Points>& path, PathKind kind) {
    const u32 records = command_count(path.count, kind);
    for (u32 i = 0; i < records; ++i) {
        commands[i].type = static_cast<unsigned char>(kind == separate_segments ? (i % 2 ? 2 : 1) :
                              i == path.count ? 5 : i ? 2 : 1);
        commands[i].control_x = commands[i].control_y = commands[i].end_x = commands[i].end_y = 0;
    }
    command_coordinates(commands, path.points, path.count, kind);
    return records;
}
template <u32 Points>
inline u32 path_coordinates(Command* commands, const draw::Path<Points>& path, PathKind kind) {
    command_coordinates(commands, path.points, Points, kind);
    return command_count(Points, kind);
}
template <u32 Commands>
inline u32 path_coordinates(Command* commands, const draw::QuadraticPath<Commands>& path, PathKind) {
    for (u32 i = 0; i < path.count; ++i) {
        const draw::QuadraticCommand& source = path.commands[i];
        Command& destination = commands[i];
        const bool quadratic = source.kind == draw::quadratic_to;
        destination.x = quadratic ? source.control.x : source.end.x;
        destination.y = quadratic ? source.control.y : source.end.y;
        destination.control_x = quadratic ? source.end.x : 0;
        destination.control_y = quadratic ? source.end.y : 0;
        destination.end_x = destination.end_y = 0;
        destination.type = static_cast<unsigned char>(source.kind);
    }
    return path.count;
}
}}
