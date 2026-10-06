#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.hpp"

int main(int argc, char** argv)
{
    doctest::Context context(argc, argv);

    context.setOption("no-version", true);
    context.setOption("success", false);

    return context.run();
}
