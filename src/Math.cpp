#include <tdk/Math.hpp>

namespace TDK
{
    namespace
    {
        struct SineTable
        {
            int16_t values[Math::AngleSteps];
        };

        constexpr double Pi = 3.14159265358979323846;

        constexpr double SineSeries(double x)
        {
            double term = x;
            double sum = x;

            for (int n = 1; n < 12; ++n)
            {
                term = -term * x * x / ((2 * n) * (2 * n + 1));
                sum += term;
            }
            return sum;
        }

        constexpr SineTable BuildSineTable()
        {
            SineTable table = {};

            for (uint32_t step = 0u; step < Math::AngleSteps; ++step)
            {
                double angle = 2.0 * Pi * step / Math::AngleSteps;

                if (angle > Pi)
                {
                    angle -= 2.0 * Pi;
                }
                const double value = SineSeries(angle) * Math::One;
                table.values[step] =
                    static_cast<int16_t>(value >= 0.0 ? value + 0.5 : value - 0.5);
            }
            return table;
        }

        constexpr SineTable sineTable = BuildSineTable();
    }

    int32_t Math::Sin(int32_t angle)
    {
        return sineTable.values[static_cast<uint32_t>(angle) & (AngleSteps - 1u)];
    }

    int32_t Math::Cos(int32_t angle)
    {
        return Sin(angle + static_cast<int32_t>(AngleSteps / 4u));
    }
}
