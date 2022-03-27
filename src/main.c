#include <stdio.h>
#include "sfmm.h"

int main(int argc, char const *argv[]) {
    void *m,*n;
    void *o,*p,*q,*r;
    m = sf_malloc(20);
    n = sf_malloc(20);
    o = sf_malloc(20);
    p = sf_malloc(20);
    q = sf_malloc(20);
    r = sf_malloc(20);


    sf_show_heap();

    sf_free(m);
    sf_free(n);
    sf_free(o);
    sf_free(p);
    sf_free(q);


   
    sf_free(r);

    sf_show_heap();

    printf("%f\n",sf_peak_utilization());

    


	// sf_free(y);
    return EXIT_SUCCESS;
}
