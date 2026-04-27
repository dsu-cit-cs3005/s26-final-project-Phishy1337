#include "RobotBase.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

class Robot_PitOracle : public RobotBase
{
private:
    struct SeenRobot
    {
        int row = -1;
        int col = -1;
        int same_spot_turns = 0;
    };

    std::set<std::pair<int, int>> m_known_mounds;
    std::set<std::pair<int, int>> m_known_flames;
    std::set<std::pair<int, int>> m_known_pits;
    std::vector<RadarObj> m_visible_robots;
    std::map<std::pair<int, int>, SeenRobot> m_robot_history;

    bool m_has_trapped_target = false;
    int m_trapped_target_row = -1;
    int m_trapped_target_col = -1;
    int m_scan_direction = 0;

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

    bool is_blocking_hazard(int row, int col) const
    {
        return m_known_mounds.count({row, col}) > 0 || m_known_flames.count({row, col}) > 0;
    }

    int distance_to_nearest_known_pit(int row, int col) const
    {
        if (m_known_pits.empty())
        {
            return std::numeric_limits<int>::max() / 4;
        }

        int best_distance = std::numeric_limits<int>::max();
        for (const auto& pit : m_known_pits)
        {
            best_distance = std::min(best_distance, chebyshev_distance(row, col, pit.first, pit.second));
        }
        return best_distance;
    }

public:
    Robot_PitOracle() : RobotBase(4, 3, grenade)
    {
        m_name = "PitOracle";
    }

    void get_radar_direction(int& radar_direction) override
    {
        radar_direction = m_scan_direction;

        // Early game: wide sweep to map hazards quickly. Then keep sweeping.
        if (m_scan_direction == 0)
        {
            m_scan_direction = 1;
        }
        else
        {
            ++m_scan_direction;
            if (m_scan_direction > 8)
            {
                m_scan_direction = 0;
            }
        }
    }

    void process_radar_results(const std::vector<RadarObj>& radar_results) override
    {
        m_visible_robots.clear();
        m_has_trapped_target = false;

        std::set<std::pair<int, int>> seen_this_turn;
        for (const RadarObj& obj : radar_results)
        {
            const std::pair<int, int> pos{obj.m_row, obj.m_col};
            if (obj.m_type == 'M')
            {
                m_known_mounds.insert(pos);
            }
            else if (obj.m_type == 'F')
            {
                m_known_flames.insert(pos);
            }
            else if (obj.m_type == 'P')
            {
                m_known_pits.insert(pos);
            }
            else if (obj.m_type == 'R')
            {
                m_visible_robots.push_back(obj);
                seen_this_turn.insert(pos);

                SeenRobot& seen = m_robot_history[pos];
                if (seen.row == obj.m_row && seen.col == obj.m_col)
                {
                    ++seen.same_spot_turns;
                }
                else
                {
                    seen.row = obj.m_row;
                    seen.col = obj.m_col;
                    seen.same_spot_turns = 1;
                }

                if (m_known_pits.count(pos) > 0 && seen.same_spot_turns >= 2)
                {
                    m_has_trapped_target = true;
                    m_trapped_target_row = obj.m_row;
                    m_trapped_target_col = obj.m_col;
                }
            }
        }

        // Decay history for robots not seen this turn so stale pit assumptions fade.
        for (auto it = m_robot_history.begin(); it != m_robot_history.end();)
        {
            if (seen_this_turn.count(it->first) == 0)
            {
                if (it->second.same_spot_turns > 0)
                {
                    --it->second.same_spot_turns;
                }
                if (it->second.same_spot_turns == 0)
                {
                    it = m_robot_history.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    bool get_shot_location(int& shot_row, int& shot_col) override
    {
        if (!m_has_trapped_target || get_grenades() <= 0)
        {
            return false;
        }

        shot_row = m_trapped_target_row;
        shot_col = m_trapped_target_col;
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

                if (is_blocking_hazard(next_row, next_col))
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

            // Primary goal: maximize distance from visible robots.
            for (const RadarObj& robot : m_visible_robots)
            {
                score += chebyshev_distance(row, col, robot.m_row, robot.m_col) * 14;
            }

            // Secondary goal: stay near pits without standing on hazards.
            const int pit_distance = distance_to_nearest_known_pit(row, col);
            if (pit_distance == 1)
            {
                score += 30;
            }
            else if (pit_distance == 2)
            {
                score += 18;
            }
            else if (pit_distance == 0)
            {
                score -= 1000;
            }
            else
            {
                score -= pit_distance;
            }

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

        move_direction = best_direction;
        move_distance = best_distance;
    }
};

extern "C" RobotBase* create_robot()
{
    return new Robot_PitOracle();
}

extern "C" const char* robot_summary()
{
    return "Evades, baits near pits, grenades trapped bots.";
}
