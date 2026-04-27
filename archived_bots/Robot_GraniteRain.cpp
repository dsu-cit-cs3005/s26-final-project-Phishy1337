#include "RobotBase.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

class Robot_GraniteRain : public RobotBase
{
private:
    int m_target_row = -1;
    int m_target_col = -1;
    int m_radar_direction = 1;

    static int manhattan_distance(int row_a, int col_a, int row_b, int col_b)
    {
        return std::abs(row_a - row_b) + std::abs(col_a - col_b);
    }

public:
    Robot_GraniteRain() : RobotBase(2, 5, grenade)
    {
        m_name = "GraniteRain";
    }

    void get_radar_direction(int& radar_direction) override
    {
        radar_direction = m_radar_direction;
        m_radar_direction = (m_radar_direction % 8) + 1;
    }

    void process_radar_results(const std::vector<RadarObj>& radar_results) override
    {
        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        int best_distance = std::numeric_limits<int>::max();
        m_target_row = -1;
        m_target_col = -1;

        for (const RadarObj& obj : radar_results)
        {
            if (obj.m_type != 'R')
            {
                continue;
            }

            const int distance = manhattan_distance(current_row, current_col, obj.m_row, obj.m_col);
            if (distance < best_distance)
            {
                best_distance = distance;
                m_target_row = obj.m_row;
                m_target_col = obj.m_col;
            }
        }
    }

    bool get_shot_location(int& shot_row, int& shot_col) override
    {
        if (get_grenades() <= 0 || m_target_row < 0 || m_target_col < 0)
        {
            return false;
        }

        shot_row = m_target_row;
        shot_col = m_target_col;
        return true;
    }

    void get_move_direction(int& move_direction, int& move_distance) override
    {
        if (m_target_row < 0 || m_target_col < 0)
        {
            move_direction = m_radar_direction;
            move_distance = 1;
            return;
        }

        int current_row = 0;
        int current_col = 0;
        get_current_location(current_row, current_col);

        const int row_delta = (m_target_row > current_row) - (m_target_row < current_row);
        const int col_delta = (m_target_col > current_col) - (m_target_col < current_col);

        for (int direction = 1; direction <= 8; ++direction)
        {
            if (directions[direction].first == row_delta &&
                directions[direction].second == col_delta)
            {
                move_direction = direction;
                move_distance = 1;
                return;
            }
        }

        move_direction = 0;
        move_distance = 0;
    }
};

extern "C" RobotBase* create_robot()
{
    return new Robot_GraniteRain();
}

extern "C" const char* robot_summary()
{
    return "Max-armor grenadier that lobs at spotted foes.";
}
