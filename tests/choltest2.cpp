// Standalone verification for SparseCholeskySolver. Rebuilds L*L^T from the
// internal factorization and compares it against the input matrix A, then
// checks the forward/backward solve reproduces the known solution.
#include "SparseCholeskySolver.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace blast_demo;

static double lAt(const SparseCholeskySolver& s, int r, int c)
{
    if (r < c) std::swap(r, c);
    const std::vector<int>& cstart = s.lColStart();
    const std::vector<int>& rindex = s.lRowIndex();
    const std::vector<double>& vals = s.lValues();
    for (int p = cstart[c]; p < cstart[c + 1]; ++p)
        if (rindex[p] == r) return vals[p];
    return 0.0;
}

int main()
{
    int failures = 0;
    const int n = 4;

    SparseCholeskySolver solver;
    for (int i = 0; i < n; ++i)
    {
        solver.addCoefficient(i, i, 4.0);
        if (i + 1 < n)
        {
            solver.addCoefficient(i, i + 1, -1.0);
            solver.addCoefficient(i + 1, i, -1.0);
        }
    }
    if (!solver.factorize())
    {
        std::fprintf(stderr, "FAIL: factorize returned false for SPD matrix\n");
        return 1;
    }

    // Rebuild A from L*L^T and compare against the analytic A.
    double maxErr = 0.0;
    for (int r = 0; r < n; ++r)
    {
        for (int c = 0; c < n; ++c)
        {
            double sum = 0.0;
            for (int k = 0; k <= (r < c ? r : c); ++k)
                sum += lAt(solver, r, k) * lAt(solver, c, k);
            double expect = 0.0;
            if (r == c) expect = 4.0;
            else if (std::abs(r - c) == 1) expect = -1.0;
            const double e = std::fabs(sum - expect);
            if (e > maxErr) maxErr = e;
        }
    }
    std::printf("max |A - L*L^T| = %.3e\n", maxErr);
    if (maxErr > 1e-9)
    {
        std::fprintf(stderr, "FAIL: L*L^T does not reproduce A\n");
        ++failures;
    }

    // Solve A*x = b for b = A*[1,2,3,4] and expect x = [1,2,3,4].
    const double x0[4] = {1.0, 2.0, 3.0, 4.0};
    double b[4] = {0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < n; ++i)
    {
        b[i] = 4.0 * x0[i];
        if (i > 0) b[i] -= x0[i - 1];
        if (i + 1 < n) b[i] -= x0[i + 1];
    }
    std::vector<double> x(n, 0.0);
    if (!solver.solve(std::vector<double>(b, b + n), x))
    {
        std::fprintf(stderr, "FAIL: solve returned false\n");
        ++failures;
    }
    else
    {
        double solveErr = 0.0;
        for (int i = 0; i < n; ++i)
            if (std::fabs(x[i] - x0[i]) > solveErr) solveErr = std::fabs(x[i] - x0[i]);
        std::printf("max |x - x0|  = %.3e\n", solveErr);
        if (solveErr > 1e-9)
        {
            std::fprintf(stderr, "FAIL: solve did not reproduce [1,2,3,4]\n");
            ++failures;
        }
    }

    // Non-SPD / negative-diagonal matrices must be rejected.
    {
        SparseCholeskySolver bad;
        bad.addCoefficient(0, 0, -1.0);
        if (bad.factorize()) { std::fprintf(stderr, "FAIL: neg diag accepted\n"); ++failures; }
    }
    {
        SparseCholeskySolver bad;
        bad.addCoefficient(0, 0, 1.0);
        bad.addCoefficient(0, 1, 2.0);
        bad.addCoefficient(1, 0, 2.0);
        bad.addCoefficient(1, 1, 1.0);
        if (bad.factorize()) { std::fprintf(stderr, "FAIL: non-PD accepted\n"); ++failures; }
    }
    {
        SparseCholeskySolver bad;
        bad.addCoefficient(0, 0, 4.0);
        bad.addCoefficient(0, 1, -1.0);
        bad.addCoefficient(1, 0, -1.0);
        bad.addCoefficient(1, 1, -5.0);
        if (bad.factorize()) { std::fprintf(stderr, "FAIL: neg diag2 accepted\n"); ++failures; }
    }

    if (failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::printf("PASS: choltest2 standalone verification\n");
    return 0;
}
