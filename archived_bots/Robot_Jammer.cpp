#include "RobotBase.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

class Robot_Jammer : public RobotBase
{
private:
    int m_target_row = -1;
    int m_target_col = -1;
    bool m_has_target = false;
    bool m_shift_phase = false;
    int m_scan_direction = 3;

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
    Robot_Jammer() : RobotBase(5, 2, railgun)
    {
        m_name = "Jammer";
    }

    void get_radar_direction(int& radar_direction) override
    {
        if (m_has_target)
        {
            int current_row = 0;
            int current_col = 0;
            get_current_location(current_row, current_col);
            const int row_delta = (m_target_row > current_row) - (m_target_row < current_row);
            const int col_delta = (m_target_col > current_col) - (m_target_col < current_col);
            const int focus_direction = choose_direction(row_delta, col_delta);
            radar_direction = focus_direction == 0 ? m_scan_direction : focus_direction;
        }
        else
        {
            radar_direction = m_scan_direction;
        }

        m_scan_direction += 2;
        if (m_scan_direction > 8)
        {
            m_scan_direction -= 8;
        }
    }

    void process_radar_results(const std::vector<RadarObj>& radar_results) override
    {
        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        int best_distance = std::numeric_limits<int>::max();
        m_has_target = false;

        for (const RadarObj& obj : radar_results)
        {
            if (obj.m_type != 'R')
            {
                continue;
            }

            const int distance =
                std::max(std::abs(obj.m_row - current_row), std::abs(obj.m_col - current_col));
            if (distance < best_distance)
            {
                best_distance = distance;
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

        shot_row = m_target_row;
        shot_col = m_target_col;
        m_shift_phase = !m_shift_phase;
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
            move_direction = m_shift_phase ? 2 : 6;
            move_distance = move_speed;
            m_shift_phase = !m_shift_phase;
            return;
        }

        int row_delta = 0;
        int col_delta = 0;

        // Reposition off-axis after each engagement to break railgun mirrors.
        if (m_shift_phase)
        {
            row_delta = (current_row < m_board_row_max - 2) ? 1 : -1;
            col_delta = (current_col < m_board_col_max - 2) ? 1 : -1;
        }
        else
        {
            row_delta = (m_target_row > current_row) - (m_target_row < current_row);
            col_delta = -((m_target_col > current_col) - (m_target_col < current_col));
        }

        move_direction = choose_direction(row_delta, col_delta);
        move_distance = move_direction == 0 ? 0 : std::min(3, move_speed);
        m_shift_phase = !m_shift_phase;
    }
};

extern "C" RobotBase* create_robot()
{
    return new Robot_Jammer();
}

extern "C" const char* robot_summary()
{
    return "Mobile railgun duelist that breaks enemy lanes.";
}
