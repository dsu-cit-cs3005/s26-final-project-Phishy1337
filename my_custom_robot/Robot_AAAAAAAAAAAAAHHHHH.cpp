#include "RobotBase.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>
#include <vector>

class Robot_AAAAAAAAAAAAAHHHHH : public RobotBase
{
private:
    int m_adjacent_target_row = -1;
    int m_adjacent_target_col = -1;
    std::vector<RadarObj> m_visible_robots;
    std::set<std::pair<int, int>> m_known_cover;
    std::set<std::pair<int, int>> m_known_dangers;
    int m_scan_direction = 0;
    int m_turn_count = 0;
    bool m_snake_flip = false;

    static int chebyshev_distance(int row_a, int col_a, int row_b, int col_b)
    {
        return std::max(std::abs(row_a - row_b), std::abs(col_a - col_b));
    }

    static int choose_direction(int row_delta, int col_delta)
    {
        for (int direction = 1; direction <= 8; ++direction)
        {
            if (directions[direction].first == row_delta &&
                directions[direction].second == col_delta)
            {
                return direction;
            }
        }
        return 0;
    }

    bool is_danger(int row, int col) const
    {
        return m_known_dangers.find({row, col}) != m_known_dangers.end();
    }

    bool is_cover(int row, int col) const
    {
        return m_known_cover.find({row, col}) != m_known_cover.end();
    }

    int cover_score(int row, int col) const
    {
        int score = 0;
        for (int delta_row = -1; delta_row <= 1; ++delta_row)
        {
            for (int delta_col = -1; delta_col <= 1; ++delta_col)
            {
                if (delta_row == 0 && delta_col == 0)
                {
                    continue;
                }

                if (is_cover(row + delta_row, col + delta_col))
                {
                    score += 120;
                }
            }
        }
        return score;
    }

public:
    Robot_AAAAAAAAAAAAAHHHHH() : RobotBase(3, 4, railgun)
    {
        m_name = "AAAAAAAAAAAAAHHHHH";
    }

    void get_radar_direction(int& radar_direction) override
    {
        radar_direction = m_scan_direction;
        m_scan_direction = (m_scan_direction + 3) % 9;
    }

    void process_radar_results(const std::vector<RadarObj>& radar_results) override
    {
        ++m_turn_count;
        m_adjacent_target_row = -1;
        m_adjacent_target_col = -1;
        m_visible_robots.clear();

        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        for (const RadarObj& obj : radar_results)
        {
            if (obj.m_type == 'R')
            {
                m_visible_robots.push_back(obj);
                if (chebyshev_distance(current_row, current_col, obj.m_row, obj.m_col) <= 1)
                {
                    m_adjacent_target_row = obj.m_row;
                    m_adjacent_target_col = obj.m_col;
                }
            }
            else if (obj.m_type == 'M' || obj.m_type == 'P' || obj.m_type == 'F' || obj.m_type == 'X')
            {
                if (obj.m_type == 'M' || obj.m_type == 'X')
                {
                    m_known_cover.insert({obj.m_row, obj.m_col});
                }

                if (obj.m_type == 'P' || obj.m_type == 'F')
                {
                    m_known_dangers.insert({obj.m_row, obj.m_col});
                }
            }
        }
    }

    bool get_shot_location(int& shot_row, int& shot_col) override
    {
        if (m_turn_count <= 25 || m_visible_robots.empty())
        {
            return false;
        }

        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        const RadarObj* closest_robot = nullptr;
        int closest_distance = std::numeric_limits<int>::max();
        for (const RadarObj& robot : m_visible_robots)
        {
            const int distance = chebyshev_distance(current_row, current_col, robot.m_row, robot.m_col);
            if (distance < closest_distance)
            {
                closest_distance = distance;
                closest_robot = &robot;
            }
        }

        if (closest_robot == nullptr)
        {
            return false;
        }

        shot_row = closest_robot->m_row;
        shot_col = closest_robot->m_col;
        return true;
    }

    void get_move_direction(int& move_direction, int& move_distance) override
    {
        const int move_speed = get_move_speed();
        if (move_speed <= 0)
        {
            move_direction = 0;
            move_distance = 0;
            return;
        }

        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        if (m_turn_count > 25 && !m_visible_robots.empty())
        {
            const RadarObj* closest_robot = nullptr;
            int closest_distance = std::numeric_limits<int>::max();
            for (const RadarObj& robot : m_visible_robots)
            {
                const int distance = chebyshev_distance(current_row, current_col, robot.m_row, robot.m_col);
                if (distance < closest_distance)
                {
                    closest_distance = distance;
                    closest_robot = &robot;
                }
            }

            if (closest_robot != nullptr)
            {
                const int base_row_delta =
                    (closest_robot->m_row > current_row) - (closest_robot->m_row < current_row);
                const int base_col_delta =
                    (closest_robot->m_col > current_col) - (closest_robot->m_col < current_col);

                int row_delta = base_row_delta;
                int col_delta = base_col_delta;

                // Zig-zag the approach so it does not sit in a clean straight lane every turn.
                if (m_snake_flip)
                {
                    if (base_row_delta == 0)
                    {
                        row_delta = (current_row < (m_board_row_max / 2)) ? 1 : -1;
                    }
                    else if (base_col_delta == 0)
                    {
                        col_delta = (current_col < (m_board_col_max / 2)) ? 1 : -1;
                    }
                    else
                    {
                        row_delta = 0;
                    }
                }
                else
                {
                    if (base_row_delta != 0 && base_col_delta != 0)
                    {
                        col_delta = 0;
                    }
                }

                int direction = choose_direction(row_delta, col_delta);
                if (direction == 0)
                {
                    direction = choose_direction(base_row_delta, base_col_delta);
                }

                if (direction != 0)
                {
                    move_direction = direction;
                    move_distance = move_speed;
                    m_snake_flip = !m_snake_flip;
                    return;
                }
            }
        }

        int best_direction = 0;
        int best_distance = 0;
        int best_score = std::numeric_limits<int>::min();

        for (int direction = 1; direction <= 8; ++direction)
        {
            int row = current_row;
            int col = current_col;
            int steps_taken = 0;
            bool unsafe = false;

            for (int step = 0; step < move_speed; ++step)
            {
                const int next_row = row + directions[direction].first;
                const int next_col = col + directions[direction].second;
                if (next_row < 0 || next_row >= m_board_row_max ||
                    next_col < 0 || next_col >= m_board_col_max)
                {
                    break;
                }

                if (is_danger(next_row, next_col) || is_cover(next_row, next_col))
                {
                    unsafe = true;
                    break;
                }

                row = next_row;
                col = next_col;
                ++steps_taken;
            }

            if (steps_taken == 0)
            {
                continue;
            }

            int score = 0;
            for (const RadarObj& robot : m_visible_robots)
            {
                score += chebyshev_distance(row, col, robot.m_row, robot.m_col) * 10;

                // Railgun bots love open lanes. Strongly avoid ending in the same row/col.
                if (row == robot.m_row)
                {
                    score -= 350;
                }
                if (col == robot.m_col)
                {
                    score -= 350;
                }
            }

            // Strongly prefer getting out of the current row and current column.
            if (row != current_row && col != current_col)
            {
                score += 1000;
            }
            else
            {
                score -= 200;
            }

            // Favor ending near cover that can block line shots.
            score += cover_score(row, col);

            // Prefer deeper movement when equally safe.
            score += steps_taken;
            if (unsafe)
            {
                score -= 10000;
            }

            if (score > best_score)
            {
                best_score = score;
                best_direction = direction;
                best_distance = steps_taken;
            }
        }

        if (best_direction == 0)
        {
            move_direction = 0;
            move_distance = 0;
            return;
        }

        move_direction = best_direction;
        move_distance = best_distance;
    }
};

extern "C" RobotBase* create_robot()
{
    return new Robot_AAAAAAAAAAAAAHHHHH();
}

extern "C" const char* robot_summary()
{
    return "Evades early, then railguns after round 25.";
}
