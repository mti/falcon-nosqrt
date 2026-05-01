#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <omp.h>
#include <atomic>
#include <iostream>
#include <map>
#include <string>
#include "Eigen/Dense"
#include "FalconKey.h"

using namespace Eigen;

static inline double linf_complex(const std::complex<double>& z)
{
    return std::max(std::abs(z.real()), std::abs(z.imag()));
}

static inline Eigen::VectorXcd round_vec_Zi(const Eigen::VectorXcd& x)
{
    // Treat x as a real vector of length 2m: [Re(x0), Im(x0), Re(x1), Im(x1), ...]
    Eigen::VectorXcd s(x.size());
    Eigen::Map<const Eigen::VectorXd> xr(
        reinterpret_cast<const double*>(x.data()), 2 * x.size());
    Eigen::Map<Eigen::VectorXd> sr(
        reinterpret_cast<double*>(s.data()), 2 * s.size());

    sr = xr.array().round().matrix();
    return s;
}

// Distance from s to the complex line <u>, i.e. || s - <u,s> u ||_2
static inline double dist_to_line(const Eigen::VectorXcd& s, const Eigen::VectorXcd& u)
{
    std::complex<double> alpha = u.dot(s);     // u^* s  (Eigen uses conjugate on lhs)
    Eigen::VectorXcd proj = alpha * u;
    return (s - proj).norm();
}

bool attempt_recovery(const VectorXcd& u, const VectorXcd& target) {
    const double r2 = 12289.0;
    const double R2 = 1.17 * 1.17 * r2;
    const double r  = std::sqrt(r2);
    const double R  = std::sqrt(R2);
    const double delta2 = std::sqrt(0.5*GEN_SIZE);
    const double Bmin = std::max(0.0, r - delta2);
    const double Bmax = R + delta2;

    std::cerr << "[recovery] try for |z| in [" << Bmin << ", " << Bmax << "]...\n";

    auto is_correct_test = [&](const Eigen::VectorXcd& s)->bool {
        // Replace with real pubkey verifier in actual attack code.
        return (s - target).cwiseAbs().maxCoeff() == 0.0;
    };

    // Enumerate Gaussian integers a with |a| <= Bmax*|u_j| + 1
    auto enumerate_S = [&](int j) {
        const double rad = Bmax * std::abs(u[j]) + 1.0;
        const long long M = (long long) std::ceil(rad);
        const double rad2 = rad*rad;

        std::vector<std::complex<double>> S;
        S.reserve((size_t)(3.5 * rad2 + 16));
        for(long long a = -M; a <= M; a++) {
            for(long long b = -M; b <= M; b++) {
                double d2 = (double)a*(double)a + (double)b*(double)b;
                if(d2 <= rad2)
                    S.emplace_back((double)a, (double)b);
            }
        }
        return S;
    };

    // Pick top indices by |u_j|
    int Jcount = 10;
    std::vector<int> idx(u.size());
    for(int i=0;i<u.size();i++)
        idx[i]=i;

    std::partial_sort(idx.begin(), idx.begin()+Jcount, idx.end(),
        [&](int a, int b){ return std::abs(u[a]) > std::abs(u[b]); });

    idx.resize(Jcount);

    const double eps = 1e-9;

    // We'll try a few pairs among the top indices. Usually (idx[0], idx[1]) is enough.
    bool found = false;
    std::complex<double> found_z(0.0, 0.0);
    Eigen::VectorXcd found_s;

    for(int p = 0; p < (int)idx.size() && !found; p++) {
        for(int q = p+1; q < (int)idx.size() && !found; q++) {

            int j = idx[p];
            int k = idx[q];

            auto Sj = enumerate_S(j);
            auto Sk = enumerate_S(k);

            /*
            std::cerr << "[recovery] trying pair (j,k)=(" << j << "," << k << ") "
                      << "|Sj|=" << Sj.size() << " |Sk|=" << Sk.size() << "\n";
            */

            const std::complex<double> uj = u[j];
            const std::complex<double> uk = u[k];
            const double denom = std::norm(uj) + std::norm(uk);

            std::atomic<bool> stop(false);
            std::complex<double> z_local(0.0, 0.0);

            // Parallelize over A (outer list); each thread scans all B
            #pragma omp parallel
            {
                // Thread-local buffers to avoid repeated allocations
                Eigen::VectorXcd x(u.size());
                Eigen::VectorXcd s(u.size());

                #pragma omp for schedule(static)
                for(long long ia = 0; ia < (long long)Sj.size(); ia++) {
                    if(stop.load(std::memory_order_relaxed)) continue;

                    const std::complex<double> A = Sj[(size_t)ia];
                    const std::complex<double> Auj = A * std::conj(uj);

                    for(const std::complex<double>& B : Sk) {
                        if(stop.load(std::memory_order_relaxed)) break;

                        const std::complex<double> z = (Auj + B * std::conj(uk)) / denom;

                        // quick annulus
                        double az = std::abs(z);
                        if(az < Bmin || az > Bmax) continue;

                        // rounding-square checks on the two coordinates
                        if(linf_complex(z*uj - A) >= 0.5 - eps) continue;
                        if(linf_complex(z*uk - B) >= 0.5 - eps) continue;

                        // build candidate s
                        x.noalias() = z * u;
                        s = round_vec_Zi(x);

                        // cheap necessary filters
                        double s2 = s.squaredNorm();
                        if(!(s2 > r2 && s2 < R2)) continue;

                        double resid = dist_to_line(s, u);
                        if(resid > delta2 + 1e-7) continue;

                        // verifier
                        if(is_correct_test(s)) {
                            // publish result
                            z_local = z;
                            #pragma omp critical
                            {
                                if(!stop.load()) {
                                    found_s = s;
                                    found_z = z;
                                    stop.store(true);
                                }
                            }
                            break;
                        }
                    }
                }
            } // end omp parallel

            if(stop.load()) {
                found = true;
                std::cerr << "[recovery] SUCCESS with pair ("
                          << j << "," << k << ")\n";
                std::cerr << "[recovery] z = " << found_z
                          << "  |z|=" << std::abs(found_z) << "\n";
                std::cerr << "[recovery] ||s||^2 = " << found_s.squaredNorm()
                          << "  dist_to_line = " << dist_to_line(found_s, u) << "\n";
            }
        }
    }
    if(!found)
        std::cerr << "[recovery] failed so far." << std::endl;
    return found;
}

void accumulate_covariance(MatrixXcd& cov, const FalconKey& key, size_t nsigs)
{
    MatrixXd  sigmatrix = key.gen_sigmatrix(nsigs);
    Map<MatrixXcd> sigmatrixcplx((std::complex<double>*)sigmatrix.data(), GEN_SIZE, nsigs);

    cov += sigmatrixcplx * sigmatrixcplx.adjoint();
}

int main(int argc, char* argv[])
{
    IOFormat SingleLineFmt(FullPrecision, DontAlignCols, ", ", ", ", "[", "]", "[", "]");
    size_t  nsigs, subspacedim = 1;
    size_t  sigs_so_far = 0, batch_size = 1024;

    if(argc < 4) {
        std::cerr << "usage: " << argv[0] << " <keyfile> <faultyval> <nsigs_step> [<batchsize>]\n";
        exit(1);
    }

    FalconKey key;
    key.load(argv[1]);
    double faultyval = strtod(argv[2], NULL);
    key.injectfault(faultyval);

    nsigs       = (size_t) strtoumax(argv[3], NULL, 0);
    if(argc >= 5)
        batch_size = (size_t) strtoumax(argv[4], NULL, 0);
    
    size_t nthrds = omp_get_max_threads();

    nsigs  = (nsigs  / nthrds + 1) * nthrds; 

    std::cerr << "Eigenspace attack with dim. " << subspacedim
              << " on " << nsigs << " faulty Falcon-" << GEN_SIZE
              << " signatures with batch size " << batch_size << std::endl;
    std::cerr << "Fault type: incorrect leftmost square root\n\n";

    VectorXd trueb0   = key.basis(all, 0);
    VectorXd b0tilde  = key.gso(all, 0);
    VectorXd b1tilde  = key.gso(all, GEN_SIZE);
    VectorXd b0tildeJ = key.gso(all, 2*GEN_SIZE-1);
    VectorXd b1tildeJ = key.gso(all, GEN_SIZE-1);

    Map<VectorXcd> trueb0c  ((std::complex<double>*)  trueb0.data(), GEN_SIZE);
    Map<VectorXcd> b0tildec ((std::complex<double>*) b0tilde.data(), GEN_SIZE);
    Map<VectorXcd> b1tildec ((std::complex<double>*) b1tilde.data(), GEN_SIZE);
    Map<VectorXcd> b0tildeJc((std::complex<double>*)b0tildeJ.data(), GEN_SIZE);
    Map<VectorXcd> b1tildeJc((std::complex<double>*)b1tildeJ.data(), GEN_SIZE);

    std::map<std::string, VectorXcd> btildes = {
        {"b0 ", b0tildec },
        {"b1 ", b1tildec },
        {"b0J", b0tildeJc}, 
        {"b1J", b1tildeJc},
    };

    std::vector<MatrixXcd> partial_covs(nthrds,
                                    MatrixXcd::Zero(GEN_SIZE, GEN_SIZE));
    MatrixXcd fullcov;
    MatrixXcd topsubspace;
    SelfAdjointEigenSolver<MatrixXcd> es;


    while(sigs_so_far < nsigs) {
#pragma omp parallel
        {
            int id;
            id = omp_get_thread_num();
            accumulate_covariance(partial_covs[id], key, batch_size);
        }
        sigs_so_far += batch_size * nthrds;

        fullcov = MatrixXcd::Zero(GEN_SIZE, GEN_SIZE);
        for(auto pcov : partial_covs)
            fullcov += pcov;

        fullcov = fullcov / (2*sigs_so_far);

        es.compute(fullcov);

        VectorXcd evfirst = es.eigenvectors().col(0);
        VectorXcd evlast  = es.eigenvectors().col(GEN_SIZE-1);

        std::cout << "Correlations after " << sigs_so_far << " signatures: " << (evlast.adjoint() * b0tildec).norm() << std::endl;
        if(attempt_recovery(evlast, trueb0c))
            return 0;
    }
    // -------------------- Recovery of s from the complex line <u> --------------------
    Eigen::VectorXcd evlast = es.eigenvectors().col(GEN_SIZE-1);;

    if(!attempt_recovery(evlast, trueb0c)) {
        std::cerr << "[recovery] Not found. Not enough signatures?\n";
        return 1;
    }

    return 0;
}
