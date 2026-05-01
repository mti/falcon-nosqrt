extern "C" {
#define restrict __restrict__
#include "inner.h"
#include "falcon.h"
}

#include "Eigen/Dense"

#define GEN_LOGN 9
#define GEN_SIZE 512

class FalconKey {
private:
    uint8_t privkey[FALCON_PRIVKEY_SIZE(GEN_LOGN)];
    uint8_t expkey[FALCON_EXPANDEDKEY_SIZE(GEN_LOGN)];
    uint8_t pubkey[FALCON_PUBKEY_SIZE(GEN_LOGN)];

    int8_t *f, *g, *F, *G;

    unsigned int bitrev(unsigned int x, int n);
    Eigen::PermutationMatrix<Eigen::Dynamic, Eigen::Dynamic> bitrev_permutation(int logn);
public:
    Eigen::VectorXd b0, b1;
    Eigen::MatrixXd basis;
    Eigen::MatrixXd gso;
    Eigen::VectorXd gsnorms;
    Eigen::PermutationMatrix<Eigen::Dynamic, Eigen::Dynamic> Prev;

    FalconKey();

    void keygen();
    void load(char* path);
    void injectfault(double faultyval);

    Eigen::MatrixXd gen_sigmatrix(size_t nsigs) const;
};
