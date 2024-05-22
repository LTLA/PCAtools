#include "Rtatami.h"
#include "pcg_random.hpp"
#include "convert_seed.h"
#include "boost/random.hpp"
#include "boost/range/algorithm.hpp"

//' @importFrom Rcpp sourceCpp
//' @useDynLib PCAtools
// [[Rcpp::export(rng=false)]]
SEXP shuffle_matrix(SEXP incoming, SEXP seed, int stream) {
    Rtatami::BoundNumericPointer bound(incoming);
    const auto& ptr = bound->ptr;
    auto NR = ptr->nrow(), NC = ptr->ncol();

    auto gen = pcg32(dqrng::convert_seed<uint64_t>(seed), stream);

    if (ptr->sparse()) {
        // Technically we could do another pass through the matrix to obtain 
        // the total number of non-zeros and avoid two allocations for 'values'
        // and 'xvec'. But this would require a second pass AND it would also
        // require some extra work to "fill-in" explicit zeros (which would be 
        // filtered out in the loop but would still occupy space in the total
        // vector). So we just pay the memory cost for speed's sake.
        std::vector<std::vector<double> > values(NC);
        std::vector<std::vector<int> > indices(NC);
        Rcpp::IntegerVector indptrs(NC + 1);

        std::vector<double> vbuffer(NR);
        auto ext = tatami::consecutive_extractor<false>(ptr.get(), false, 0, NC);

        for (int c = 0; c < NC; ++c) {
            auto ptr = ext->fetch(c, vbuffer.data());
            tatami::copy_n(ptr, NR, vbuffer.data());
            boost::range::random_shuffle(vbuffer, gen);
            for (int r = 0; r < NR; ++r) {
                if (vbuffer[r]) {
                    values[c].push_back(vbuffer[r]);
                    indices[c].push_back(r);
                }
            }
            indptrs[c + 1] = indptrs[c] + values[c].size();
        }

        size_t total = indptrs[NC];
        Rcpp::NumericVector xvec(total);
        Rcpp::IntegerVector ivec(total);
        for (int c = 0; c < NC; ++c) {
            std::copy(values[c].begin(), values[c].end(), xvec.begin() + indptrs[c]);
            std::copy(indices[c].begin(), indices[c].end(), ivec.begin() + indptrs[c]);
        }

        Rcpp::S4 output("dgCMatrix");
        output.slot("x") = xvec;
        output.slot("i") = ivec;
        output.slot("p") = indptrs;
        output.slot("Dim") = Rcpp::IntegerVector::create(NR, NC);
        output.slot("Dimnames") = Rcpp::List::create(R_NilValue, R_NilValue);
        output.slot("factors") = Rcpp::List();

        return output;

    } else {
        Rcpp::NumericMatrix output(NR, NC);
        double* optr = static_cast<double*>(output.begin());
        auto ext = tatami::consecutive_extractor<false>(ptr.get(), false, 0, NC);
        std::vector<double> buffer(NR);

        for (int c = 0; c < NC; ++c, optr += NR) {
            auto ptr = ext->fetch(c, buffer.data());
            tatami::copy_n(ptr, NR, buffer.data());
            boost::range::random_shuffle(buffer, gen);
            std::copy(buffer.begin(), buffer.end(), optr); // double-copy (no std::span, unfortunately).
        }

        return output;
    }
}
