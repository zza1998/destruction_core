#include "SparseCholeskySolver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

namespace blast_demo
{
void SparseCholeskySolver::addCoefficient(int row, int col, double value)
{
    if (row < 0 || col < 0) return;
    // Keep the matrix symmetric: store both (row,col) and (col,row) in the
    // lower-triangle CSR later. For now just record the raw triplets.
    m_raw.emplace_back(row, col, value);
}

bool SparseCholeskySolver::buildCsr()
{
    // Determine dimension from the largest index seen.
    m_n = 0;
    for (const auto& t : m_raw) m_n = std::max(m_n, std::max(t.row, t.col) + 1);
    if (m_n == 0) return false;

    // Map lower-triangle entries only (r >= c). The upper triangle is the
    // mirror of the lower one by symmetry, so including it would double the
    // off-diagonal values. CSR stores row-major: row index = r, column = c.
    std::map<long long, double> lower;
    for (const auto& t : m_raw)
    {
        const int r = t.row, c = t.col;
        if (r < c) continue;          // upper triangle: mirror of lower
        lower[static_cast<long long>(r) * m_n + c] += t.value;
    }

    // Build CSR for the lower triangle (row-major, sorted columns).
    m_rowStart.assign(m_n + 1, 0);
    m_colIndex.clear();
    m_values.clear();
    std::map<int, std::set<int>> rowCols;
    std::map<std::pair<int, int>, double> rowVals;
    for (const auto& kv : lower)
    {
        const int lo = static_cast<int>(kv.first / m_n);
        const int hi = static_cast<int>(kv.first % m_n);
        rowCols[lo].insert(hi);
        rowVals[{lo, hi}] = kv.second;
    }
    for (int row = 0; row < m_n; ++row)
    {
        m_rowStart[row] = static_cast<int>(m_colIndex.size());
        for (int col : rowCols[row])
        {
            m_colIndex.push_back(col);
            m_values.push_back(rowVals[{row, col}]);
        }
    }
    m_rowStart[m_n] = static_cast<int>(m_colIndex.size());
    m_nonzeros = static_cast<int>(m_colIndex.size());
    return true;
}

bool SparseCholeskySolver::numericFactorize()
{
    // Dense column-oriented Cholesky LL^T. The matrix is small for this demo,
    // so a dense working vector per column is clear and robust:
    //   for k:  work[i] = A[i][k]  (i >= k)
    //           work[i] -= L[k][j] * L[i][j]  for j < k where L[k][j] != 0
    //           L[k][k] = sqrt(work[k]);  L[i][k] = work[i] / L[k][k]
    m_lColStart.assign(m_n + 1, 0);
    m_lRowIndex.clear();
    m_lValues.clear();
    m_lDiag.assign(m_n, 0.0);

    std::vector<double> work(m_n, 0.0);

    for (int k = 0; k < m_n; ++k)
    {
        // Mark the start of column k in the L storage. This must happen
        // before the update loop so that the previous column's span
        // [m_lColStart[k-1], m_lColStart[k]) is already closed off.
        m_lColStart[k] = static_cast<int>(m_lRowIndex.size());

        // Column k of A (lower triangle), rows i >= k.
        for (int i = k; i < m_n; ++i) work[i] = 0.0;
        for (int r = k; r < m_n; ++r)
        {
            for (int p = m_rowStart[r]; p < m_rowStart[r + 1]; ++p)
                if (m_colIndex[p] == k) work[r] = m_values[p];
        }

        // Subtract contributions from earlier columns j where L[k][j] != 0.
        for (int j = 0; j < k; ++j)
        {
            // Find L[k][j].
            double lkj = 0.0;
            for (int p = m_lColStart[j]; p < m_lColStart[j + 1]; ++p)
                if (m_lRowIndex[p] == k) { lkj = m_lValues[p]; break; }
            if (lkj == 0.0) continue;
            for (int p = m_lColStart[j]; p < m_lColStart[j + 1]; ++p)
            {
                const int i = m_lRowIndex[p];
                if (i >= k) work[i] -= lkj * m_lValues[p];
            }
        }

        // Diagonal pivot; fail if the Schur complement is non-positive.
        const double pivot = work[k];
        if (!(pivot > 1e-14))
            return false;
        m_lDiag[k] = std::sqrt(pivot);

        // Store column k: diagonal plus rows i>k with nonzero L[i][k].
        m_lRowIndex.push_back(k);
        m_lValues.push_back(m_lDiag[k]);
        for (int i = k + 1; i < m_n; ++i)
        {
            const double l = work[i] / m_lDiag[k];
            if (std::fabs(l) > 1e-15)
            {
                m_lRowIndex.push_back(i);
                m_lValues.push_back(l);
            }
        }
    }
    m_lColStart[m_n] = static_cast<int>(m_lRowIndex.size());
    return true;
}

bool SparseCholeskySolver::factorize()
{
    m_factorized = false;
    if (!buildCsr()) return false;
    m_raw.clear();            // raw triplets no longer needed after CSR build
    if (!numericFactorize()) return false;
    m_factorized = true;
    return true;
}

bool SparseCholeskySolver::solve(const std::vector<double>& b, std::vector<double>& x) const
{
    if (!m_factorized) return false;
    if (b.size() != static_cast<size_t>(m_n)) return false;
    x.assign(m_n, 0.0);

    // Forward substitution: L * y = b. Column-oriented: for column j, divide
    // the diagonal to get y[j], then subtract L[i][j]*y[j] from rows i>j.
    std::vector<double> y = b;
    for (int j = 0; j < m_n; ++j)
    {
        y[j] /= m_lDiag[j];
        for (int p = m_lColStart[j]; p < m_lColStart[j + 1]; ++p)
        {
            const int i = m_lRowIndex[p];
            if (i == j) continue;
            y[i] -= m_lValues[p] * y[j];
        }
    }

    // Backward substitution: L^T * x = y.
    for (int i = m_n - 1; i >= 0; --i)
    {
        double sum = y[i];
        for (int p = m_lColStart[i]; p < m_lColStart[i + 1]; ++p)
        {
            const int row = m_lRowIndex[p];
            if (row == i) continue;
            sum -= m_lValues[p] * x[row];
        }
        x[i] = sum / m_lDiag[i];
    }
    return true;
}

SparseCholeskySolver::Entry::Entry(int r, int c, double v) : row(r), col(c), value(v) {}
}
