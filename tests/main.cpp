#define DOCTEST_CONFIG_IMPLEMENT
#include "doctest.hpp"

int main(int argc, char** argv)
{
    doctest::Context context;

    context.setOption("no-version", true);
    context.setOption("success", false);
    //context.setOption("duration", true);

    context.applyCommandLine(argc, argv);

    return context.run();
}
