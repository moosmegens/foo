#ifndef FOO_SCOPED_TIMER_HPP
#define FOO_SCOPED_TIMER_HPP


#include <chrono>
#include <iostream>
#include <ostream>
#include <string>


namespace foo
{
    class scoped_timer
    {
        using clock = std::chrono::steady_clock;

    private:
        std::string name;
        clock::time_point start_time;
        std::ostream& stream = std::cout;
        bool log = true;

    public:
        explicit scoped_timer(std::string name, bool do_log = true, std::ostream& stream = std::cout)
            : name(std::move(name)), stream(stream), log(do_log)
        {
            start_time = clock::now();
            if (log)
            {
                stream << "[" << this->name << "] started" << std::endl;
            }
        }

        explicit scoped_timer() :
            log(false)
        {
            start_time = clock::now();
        }

        scoped_timer(const scoped_timer&) = delete;
        scoped_timer(scoped_timer&&) = delete;
        scoped_timer& operator=(const scoped_timer&) = delete;
        scoped_timer& operator=(scoped_timer&&) = delete;

        ~scoped_timer()
        {
            if (log)
            {
                stream << '[' << name << "] completed in " << format(elapsed<std::chrono::nanoseconds>()) << '\n';
            }
        }

        void do_log(const bool do_log = false)
        {
            log = do_log;
        }

        template <class Duration = std::chrono::nanoseconds>
        [[nodiscard]] Duration elapsed() const noexcept
        {
            return std::chrono::duration_cast<Duration>(clock::now() - start_time);
        }

        static std::string format(std::chrono::nanoseconds d)
        {
            long long n = d.count();
            std::ostringstream os;

            os << std::fixed << std::setprecision(0);

            if (n < 1'000)
                os << n << " ns";
            else if (n < 1'000'000)
                os << static_cast<double>(n) / 1e3 << " us";
            else if (n < 1'000'000'000)
                os << static_cast<double>(n) / 1e6 << " ms";
            else
                os << static_cast<double>(n) / 1e9 << " s";
            return os.str();
        }
    };
}


#endif //FOO_SCOPED_TIMER_HPP
