#include "SparseCholeskySolver.h"

#include <cmath>
#include <iostream>

int main()
{
    using blast_demo::SparseCholeskySolver;
    int failures = 0;

    // Tri-diagonal SPD matrix (4x4): A[i][i]=4, A[i][i+1]=A[i+1][i]=-1.
    // Known solution: A * x = b, pick x0 = [1,2,3,4], compute b, then solve.
    SparseCholeskySolver solver;
    const int n = 4;
    for (int i = 0; i < n; ++i)
    {
        solver.addCoefficient(i, i, 4.0);
        if (i + 1 < n)
        {
            solver.addCoefficient(i, i + 1, -1.0);
            solver.addCoefficient(i + 1, i, -1.0);
        }
    }
    const double x0[4] = {1.0, 2.0, 3.0, 4.0};
    double b[4] = {0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < n; ++i)
    {
        b[i] = 4.0 * x0[i];
        if (i > 0) b[i] -= x0[i - 1];
        if (i + 1 < n) b[i] -= x0[i + 1];
    }

    if (!solver.factorize())
    {
        std::cerr << "FAIL: factorize() returned false for SPD matrix\n";
        return 1;
    }
    std::vector<double> x(n, 0.0);
    const std::vector<double> bVec(b, b + n);
    if (!solver.solve(bVec, x))
    {
        std::cerr << "FAIL: solve() returned false\n";
        return 1;
    }
    for (int i = 0; i < n; ++i)
    {
        if (std::fabs(x[i] - x0[i]) > 1e-9)
        {
            std::cerr << "FAIL: x[" << i << "] = " << x[i] << ", expected " << x0[i] << "\n";
            ++failures;
        }
    }

    // Non-SPD matrix (negative diagonal) must fail factorize.
    {
        SparseCholeskySolver bad;
        bad.addCoefficient(0, 0, -1.0);
        if (bad.factorize())
        {
            std::cerr << "FAIL: non-SPD matrix factorized successfully\n";
            ++failures;
        }
    }

    if (failures)
    {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "PASS: sparse cholesky solver\n";
    return 0;
}
