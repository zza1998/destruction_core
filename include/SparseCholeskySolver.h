#pragma once

#include <cstddef>
#include <vector>

namespace blast_demo
{
// A self-contained sparse direct solver for symmetric positive-definite
// systems A*x = b using Cholesky factorization (A = L * L^T).
//
// Usage:
//   1. Build the matrix by calling addCoefficient(row, col, value) for every
//      nonzero of the lower triangle (or both, the symmetric mirror is added
//      automatically).
//   2. Call factorize() once.
//   3. Call solve(b, x) for each right-hand side; the factorization is reused.
//
// The matrix is stored in compressed sparse row (CSR) form. Factorization
// performs a simple symbolic pass (diagonal dominance ordering is preserved as
// given) followed by a numeric Cholesky decomposition with a small diagonal
// regularization fallback so near-singular SPD systems still solve.
class SparseCholeskySolver
{
public:
    // Add one matrix entry. Values on the upper triangle are mirrored to the
    // lower triangle automatically (matrix is assumed symmetric).
    void addCoefficient(int row, int col, double value);

    // Finalize the sparse structure and factorize A = L*L^T. Returns true on
    // success. A non-positive-definite matrix fails here.
    bool factorize();

    // Solve A * x = b into x. Requires a successful factorize().
    bool solve(const std::vector<double>& b, std::vector<double>& x) const;

    int size() const { return m_n; }
    int nonzeroCount() const { return m_nonzeros; }

    // Read-only inspection of the factorization (column-major columns of L).
    // Useful for verification and debugging. Empty before factorize().
    const std::vector<int>& lColStart() const { return m_lColStart; }
    const std::vector<int>& lRowIndex() const { return m_lRowIndex; }
    const std::vector<double>& lValues() const { return m_lValues; }
    const std::vector<double>& lDiag() const { return m_lDiag; }

private:
    struct Entry
    {
        int row;
        int col;
        double value;
        Entry(int r, int c, double v);
    };
    // CSR storage of the lower triangle (rows 0..m_n-1).
    int m_n = 0;
    std::vector<Entry> m_raw;      // accumulated raw triplets before factorize
    std::vector<int> m_rowStart;   // size m_n+1
    std::vector<int> m_colIndex;   // sorted per row
    std::vector<double> m_values;  // matrix values (lower triangle)
    // Factorization storage: L values (column-major columns of L, sparse).
    std::vector<int> m_lColStart;
    std::vector<int> m_lRowIndex;
    std::vector<double> m_lValues;
    std::vector<double> m_lDiag;   // L[i][i]
    bool m_factorized = false;
    int m_nonzeros = 0;

    bool buildCsr();
    bool numericFactorize();
};
}
