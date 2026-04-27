#include "RobotBase.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

class Robot_PitViper : public RobotBase
{
private:
    int m_target_row = -1;
    int m_target_col = -1;
    bool m_has_target = false;
    int m_scan_direction = 7;

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

public:
    Robot_PitViper() : RobotBase(4, 3, flamethrower)
    {
        m_name = "PitViper";
    }

    void get_radar_direction(int& radar_direction) override
    {
        radar_direction = m_scan_direction;
        if (m_has_target)
        {
            int current_row = 0;
            int current_col = 0;
            get_current_location(current_row, current_col);
            const int row_delta = (m_target_row > current_row) - (m_target_row < current_row);
            const int col_delta = (m_target_col > current_col) - (m_target_col < current_col);
            const int target_direction = choose_direction(row_delta, col_delta);
            if (target_direction != 0)
            {
                radar_direction = target_direction;
            }
        }

        if (m_scan_direction == 7)
        {
            m_scan_direction = 3;
        }
        else if (m_scan_direction == 3)
        {
            m_scan_direction = 0;
        }
        else
        {
            m_scan_direction = 7;
        }
    }

    void process_radar_results(const std::vector<RadarObj>& radar_results) override
    {
        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        m_has_target = false;
        int best_score = std::numeric_limits<int>::min();

        for (const RadarObj& obj : radar_results)
        {
            if (obj.m_type != 'R')
            {
                continue;
            }

            const int distance = chebyshev_distance(current_row, current_col, obj.m_row, obj.m_col);
            int score = 100 - distance;

            // Strong bias toward wall-huggers like Ratboy.
            if (obj.m_col <= 1)
            {
                score += 50;
            }

            if (score > best_score)
            {
                best_score = score;
                m_target_row = obj.m_row;
                m_target_col = obj.m_col;
                m_has_target = true;
            }
        }
    }

    bool get_shot_location(int& shot_row, int& shot_col) override
    {
        if (!m_has_target)
        {
            return false;
        }

        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);
        if (chebyshev_distance(current_row, current_col, m_target_row, m_target_col) > 4)
        {
            return false;
        }

        shot_row = m_target_row;
        shot_col = m_target_col;
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

        if (!m_has_target)
        {
            // Drift toward the left edge to ambush Ratboy lanes.
            const int col_delta = (0 > current_col) - (0 < current_col);
            const int direction = choose_direction(0, col_delta);
            move_direction = direction == 0 ? 7 : direction;
            move_distance = std::min(move_speed, std::max(1, current_col));
            return;
        }

        int intercept_col = m_target_col;
        if (m_target_col <= 1)
        {
            intercept_col = 0;
        }

        const int row_delta = (m_target_row > current_row) - (m_target_row < current_row);
        const int col_delta = (intercept_col > current_col) - (intercept_col < current_col);
        move_direction = choose_direction(row_delta, col_delta);
        move_distance = move_direction == 0 ? 0 : move_speed;
    }
};

extern "C" RobotBase* create_robot()
{
    return new Robot_PitViper();
}

extern "C" const char* robot_summary()
{
    return "Flanks wall-huggers and burns them up close.";
}
