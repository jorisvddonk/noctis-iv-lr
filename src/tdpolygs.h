/*

    The Third Flare.
    ----------------
    Full-3D Graphic Engine.

    Funzioni a 32-bit per disegnare poligoni 3d riempiti di colore
    in grafica 320x200x256. Sul Pentium � una cannonata: una media
    di 12000 poligoni al secondo, da workstation, con 75 MhZ.

    - Non si deve mai usare il colore nr. 255 per
      tracciare qualcosa nell'area di lavoro, quando si usa questa
      routine, perch� lo utilizza solo lei per delimitare i poligoni.
      Se si vuole usare un altro colore per delimitarli, bisogna
      cambiare tutti i valori 255 nelle funzioni "Segmento" e "poly3d".

    Chiamare initscanlines prima di tracciare alcunch�,
    senn� non traccia una bella sega.

    Nato (incredibile a dirsi) per Alpha Dream.
    Ultima revisione 21 Dicembre 1997.

    Aggiunte: ottimizzazione conversioni 3d-2d, 23.6.1997;
          ciclo di fast-load dei vertici quando sono tutti di fronte
          alla videocamera (cio� quasi sempre), 23.6.1997;
          clipping intelligente, ottobre '97;
          texture mapping, febbraio '98.

*/

#include <math.h>
#include <memory.h>
#include <stdio.h>

#include "noctis-d.h"

/* Dati sui poligoni (quanti vertici al massimo si pensa di fare). */

#ifndef VERTICI_PER_POLIGONO
#define VERTICI_PER_POLIGONO 4
#endif

/* Area di visualizzazione. */

#ifndef larghezza
#define larghezza 300
#endif

#ifndef altezza
#define altezza 180
#endif

#ifndef x_centro
#define x_centro 160
#endif

#ifndef y_centro
#define y_centro 100
#endif

/*

    Controllo visualizzazione.

*/

float uneg = 100; /* Un fatto particolarmente curioso: uneg rappresenta
           la distanza minima alla quale un certo punto non viene
           considerato alle spalle dell'osservatore. Il bello �
           che l'ideazione di questa sigla (ricordo che si
           pronuncia u-neg) risale a cos� tanto tempo fa
           (si parla della prima versione di Smallest, ovvero
           dell'aprile '92, attualmente pi� di 5 anni fa)
           che non ricordo assolutamente che cosa significhi
           quest'acronimo (era un acronimo, ma chiss� come mi
           venne in mente, forse da user-neg, negativo all'utente,
           in riferimento all'effetto di inversione del segno
           dato dalle funzioni prospettiche quando, per l'appunto,
           una tripletta di coordinate ha la distanza normale
           dallo schermo in negativo, ma non sono affatto sicuro. */

float alfa = 0, beta = 0, ngamma = 0;
float cam_x = 0, cam_y = 0, cam_z = 0;

const double deg = M_PI / 180;

// Distanza dal piano di proiezione.

float dpp     = 200;
float inv_dpp = 1 / 200;

/*  Costanti e variabili di controllo del texture mapping.
    H_MATRIXS e V_MATRIXS specificano le ripetizioni della stessa
    texture sui poligoni, ma per avere la possibilit� di dimezzare
    la texture o di disegnarne solo un quarto, sono state assunte
    come rappresentanti di "quarti di texture". Significa che
    per avere un texturing normale, senza ripetizioni n�
    dimezzamenti, bisogna assegnare le due variabili a 4,
    non a 1 (che rappresenterebbe un solo quarto).
    E cos� via... se si mette 8, si ottiene la ripetizione
    della texture per due volte sul corrispondente asse,
    se si mette a 12 si ottengono tre ripetizioni, eccetera...
    (ps ho cambiato la base a 16, sedicesimi di texture). */

const uint16_t MPIX          = 319; // Massimo PIXEL su X.
const uint16_t MPIY          = 199; // Massimo PIXEL su Y.
const uint16_t TEXTURE_XSIZE = 256; // Larghezza delle BMP.
const uint16_t TEXTURE_YSIZE = 256; // Altezza delle BMP.

float EMU_K       = 16;                        // Cost. di emulaz. FPU
int32_t H_MATRIXS = 16;                        // Nr. ripetizioni. 16-128
int32_t V_MATRIXS = 16;                        // Nr. ripetizioni. 16-128
int32_t XSIZE     = TEXTURE_XSIZE * H_MATRIXS; // Calibraz. dimensioni.
int32_t YSIZE     = TEXTURE_YSIZE * V_MATRIXS; // Calibraz. dimensioni.
float XCOEFF      = EMU_K / dpp;               // Coefficente di comodo.
float YCOEFF      = EMU_K / dpp;               // Coefficente di comodo.

// Initialize scanline addresses.

uint16_t riga[200];

void initscanlines() {
    uint16_t c;

    for (c = 0; c < 200; c++) {
        riga[c] = static_cast<uint16_t>(320 * c);
    }
}

// Draw a segment without checking limits. Faster.
// Trace with color 255.
// It does not carry out checks, so it should not be used for other things,
// or at least you have to be careful to include the extremes of the
// segment in the usable video area.
// The one doing the checks is in the waveline function of
// Liquid Player, and it is safe at 1000 per thousand.

uint16_t ptr;

uint32_t xp, yp, xa, ya;
uint32_t global_x, global_y;

void Segmento() {
    int32_t a, b, L;
    uint32_t pi, pf;
    bool flip = false;

    if (xp == xa) {
        if (ya >= yp) {
            pi = riga[yp] + xp;
            pf = riga[ya + 1];
        } else {
            pi = riga[ya] + xp;
            pf = riga[yp + 1];
        }

        while (pi < pf) {
            adapted[pi] = 255;
            pi += 320;
        }

        return;
    }

    pi = abs((int32_t)xa - (int32_t)xp);
    if (xa < xp) {
        uint32_t temp;

        temp = xp;
        xp   = xa;
        xa   = temp;

        temp = yp;
        yp   = ya;
        ya   = temp;
    }

    a = pi;
    L = pi;

    b = abs((int32_t)ya - (int32_t)yp);
    if (ya < yp) {
        flip = true;
    }

    if (b >= L) {
        L = b;
    }

    L++;
    xa <<= 16u;

    global_x = xp << 16u;
    global_y = yp << 16u;

    a *= 65536;
    b *= 65536;

    a /= L;
    b /= L;

    if (flip)
        b = -b;

    while (global_x < xa) {
        uint16_t argblarg = 2 * (global_y >> 16u);
        uint16_t index    = (global_x >> 16u);

        global_x += a;
        global_y += b;

        auto *charRiga = (uint8_t *)riga;
        uint16_t shift = (charRiga[argblarg + 1] << 8u) + charRiga[argblarg];
        index += shift;

        adapted[index] = 255;
    }
}

// La funzione di tracciamento viene principalmente usata da Noctis,
// ed una delle sue caratteristiche � di poter fare poligoni semitrasparenti
// che simulano degli aloni luminosi: si attiva con il flag "flares".
// Perch� il disegno dei bordi (dei segmenti) non interferisca con tale
// operazione, serve una pagina sana, priva di bordi, diversa da quella su
// cui si va a tracciare.
// Per default, � la seguente, posta a nulla perch� tdpolygs non si
// prepara subito a questa particolare funzione.

int8_t flares = 0;

int32_t mp[6 * VERTICI_PER_POLIGONO];

uint16_t max_x, max_y, min_x, min_y;

// Dati globali per conversioni, rototraslazioni e clipping.

int8_t doflag;
int16_t pvert, nvert, vvert;
int16_t vr, vr2, vr3, vr4, vr5, vr6;

float zk;
float x2, y2, z2;
float xx, yy, zz;

/*

    Voglio annotarmi le sigle complicate, senn� finisce come per u-neg.

    doflag   = DO-(or-not)-for-each-polygon-FLAG.
           da quando c'� il fast-load funge anche come contatore
           per i vertici da non ritagliare a livello 3d.
           infatti, se sono tutti in doflag, il poligono non
           interseca lo schermo e si pu� lasciare com'�, almeno
           a livello 3d.
    x2,y2,z2 = variabili di transito nelle rotazioni.
    pvert    = Previous-VERTex.
    nvert    = Next-VERTex.
    vvert    = bella domanda, mi sono scordato anche questa.
           venuto in mente: Visible VERTex, vertice visibile.
    zk       = Zeta-Constant... costante derivata dal calcolo della
           differenza tra coordinate zeta normalizzate allo schermo,
           viene usata nel clipping a livello 3d dalla funz. "poly3d".
           in realt� � una costante solo per due equazioni,
           ma � meglio calcolarla una volta sola che due.

    xx,yy,zz = in questa inclusione non sono mai usate, ma ci sono sempre
           state perch� una volta servivano a qualcosa (non ricordo
           per nulla a cosa). attualmente, devono rimanerci perch�
           molti programmi le usano come variabili temporanee.

    pun = PUNto, � un contatore/non esiste pi� dacch� ho tradotto in asm.
    rxf = Rototraslated-X-Floating-point-value.
    rwf = Rototraslated-vieW-(or-not)-Flag.
    mp  = Matrice Punti.
    lbx = LowerBound-X.
    ubx = UpperBound-X.

    opt_tsinalfa = OPTimized-Table-of-SINe-for-ALFA-angle.
    opt_psinalfa = OPTimized-Projected-table-of-SINe-for-ALFA-angle.

*/

int8_t rwf[VERTICI_PER_POLIGONO];

float rxf[VERTICI_PER_POLIGONO];
float ryf[VERTICI_PER_POLIGONO];
float rzf[VERTICI_PER_POLIGONO];

// Limiti dell'area video rielaborati.

#define lbx -larghezza / 2 + x_centro
#define ubx larghezza / 2 + x_centro
#define lby -altezza / 2 + y_centro
#define uby altezza / 2 + y_centro

int32_t lbxl = lbx;
int32_t ubxl = ubx;
int32_t lbyl = lby;
int32_t ubyl = uby;

float lbxf = lbx;
float ubxf = ubx;
float lbyf = lby;
float ubyf = uby;

float x_centro_f = x_centro;
float y_centro_f = y_centro;

// Chiamare dopo aver cambiato l'angolo visuale.
// Aggiorna le seguenti variabili di ottimizzazione, usate da poly3d.

float opt_pcosbeta = dpp;
float opt_psinbeta = 0;
float opt_tcosbeta = 1;
float opt_tsinbeta = 0;
float opt_pcosalfa = dpp;
float opt_psinalfa = 0;
float opt_tcosalfa = 1;
float opt_tsinalfa = 0;

float opt_tcosngamma = 1;
float opt_tsinngamma = 0;

void change_angle_of_view() {
    opt_pcosbeta   = cos(beta * deg) * dpp;
    opt_psinbeta   = sin(beta * deg) * dpp;
    opt_tcosbeta   = cos(beta * deg);
    opt_tsinbeta   = sin(beta * deg);
    opt_pcosalfa   = cos(alfa * deg) * dpp; // 0.833
    opt_psinalfa   = sin(alfa * deg) * dpp; // 0.833
    opt_tcosalfa   = cos(alfa * deg);
    opt_tsinalfa   = sin(alfa * deg);
    opt_tcosngamma = cos(ngamma * deg);
    opt_tsinngamma = sin(ngamma * deg);
}

// Chiamare dopo aver cambiato dpp.
// Cambia l'obiettivo della videocamera.

void change_camera_lens() {
    inv_dpp = 1 / dpp;
    XCOEFF  = EMU_K / dpp;
    YCOEFF  = EMU_K / dpp;
    change_angle_of_view();
}

// Chiamare dopo aver cambiato V_MATRIXS o H_MATRIXS.
// Aggiorna le variabili di lavoro per il texture mapping.

void change_txm_repeating_mode() {
    XSIZE = TEXTURE_XSIZE * H_MATRIXS;
    YSIZE = TEXTURE_YSIZE * V_MATRIXS;
}

/*  Questa � la funzione da invocare per ogni poligono da tracciare.

    E` un vero capolavoro: nessun problema, massima ottimizzazione,
    al 99% puro assembly per coprocessori da 387 in su e codice 32-bit,
    probabilmente la pi� complessa e bella funzione ch'io abbia mai
    partorito.

    Chiunque tenter� di capirci qualcosa rimarr� molto scoraggiato.
    Lungo e complesso, ma provate ad eseguirlo ed a monitorarne il flusso:
    vi accorgerete della sua enorme agilit�. */

float uno = 1; // sempre uno: � una costante di comodo.

uint8_t entity = 1; /* controlla quantit� generiche nel riempimento
                 dei poligoni con alcuni effetti speciali. */

void poly3d(const float *x, const float *y, const float *z, uint16_t nrv,
            uint8_t colore) {
    uint16_t _8n;
    uint8_t ent = entity;
    // 3D Matrices: Everything on the stack
    float ultima_x[2 * VERTICI_PER_POLIGONO];
    float ultima_y[2 * VERTICI_PER_POLIGONO];
    float ultima_z[2 * VERTICI_PER_POLIGONO];

    // 2D Matrices: Same as above.
    float video_x0[2 * VERTICI_PER_POLIGONO], video_y0[2 * VERTICI_PER_POLIGONO];
    float video_x1[3 * VERTICI_PER_POLIGONO], video_y1[3 * VERTICI_PER_POLIGONO];
    float video_x2[4 * VERTICI_PER_POLIGONO], video_y2[4 * VERTICI_PER_POLIGONO];
    float video_x3[6 * VERTICI_PER_POLIGONO], video_y3[6 * VERTICI_PER_POLIGONO];
    // Rototranslations of the vertices; the data still remains 3D.

    doflag = 0;
    for (uint16_t i = 0; i < nrv; i++) {
        zz = z[i] - cam_z;
        xx = x[i] - cam_x;
        yy = y[i] - cam_y;

        z2 = (zz * opt_tcosbeta) - (xx * opt_tsinbeta);

        rxf[i] = (zz * opt_psinbeta) + (xx * opt_pcosbeta);
        rzf[i] = (z2 * opt_tcosalfa) + (yy * opt_tsinalfa);
        ryf[i] = (yy * opt_pcosalfa) - (z2 * opt_psinalfa);

        if (rzf[i] >= uneg) {
            doflag++;
            rwf[i] = 1;
        } else {
            rwf[i] = 0;
        }
    }

    if (!doflag) {
        return;
    }

    // Fast-load for completely visible polygons.
    if (doflag == nrv) {
        vr2 = nrv;
        memcpy(ultima_x, rxf, nrv * sizeof(float));
        memcpy(ultima_y, ryf, nrv * sizeof(float));
        memcpy(ultima_z, rzf, nrv * sizeof(float));
    } else {
        // Convert points behind the observer compared to the screen plane
        uint16_t fakedi = 0;
        for (vr = 0; vr < nrv; vr++) {
            if (rwf[vr] != 0) {
                ultima_x[fakedi / 4] = rxf[vr];
                ultima_y[fakedi / 4] = ryf[vr];
                ultima_z[fakedi / 4] = rzf[vr];
                fakedi += 4;
                continue;
            }

            if (vr >= 1) {
                pvert = vr - 1;
            } else {
                pvert = nrv - 1;
            }

            if ((vr + 1) <= (nrv - 1)) {
                nvert = vr + 1;
            } else {
                nvert = 0;
            }

            if (rwf[pvert] == 0 && rwf[nvert] == 0)
                continue;

            if (rwf[pvert] + rwf[nvert] == 2) {
                if (rzf[vr] == rzf[pvert]) {
                    ultima_x[fakedi / 4] = rxf[vr];
                    ultima_y[fakedi / 4] = ryf[vr];
                } else {
                    zk = (uneg - rzf[pvert]) / (rzf[vr] - rzf[pvert]);

                    ultima_x[fakedi / 4] = zk * (rxf[vr] - rxf[pvert]) + rxf[pvert];
                    ultima_y[fakedi / 4] = zk * (ryf[vr] - ryf[pvert]) + ryf[pvert];
                }
                ultima_z[fakedi / 4] = uneg;

                if (rzf[vr] == rzf[nvert]) {
                    ultima_x[(fakedi / 4) + 1] = rxf[vr];
                    ultima_y[(fakedi / 4) + 1] = ryf[vr];
                } else {
                    zk = (uneg - rzf[nvert]) / (rzf[vr] - rzf[nvert]);

                    ultima_x[(fakedi / 4) + 1] =
                        zk * (rxf[vr] - rxf[nvert]) + rxf[nvert];
                    ultima_y[(fakedi / 4) + 1] =
                        zk * (ryf[vr] - ryf[nvert]) + ryf[nvert];
                }
                ultima_z[(fakedi / 4) + 1] = uneg;
                fakedi += 8;
            } else {
                if (rwf[pvert] == 0) {
                    vvert = nvert;
                } else {
                    vvert = pvert;
                }

                if (rzf[vvert] == rzf[vr]) {
                    ultima_x[fakedi / 4] = rxf[vr];
                    ultima_y[fakedi / 4] = ryf[vr];
                } else {
                    zk = (uneg - rzf[vvert]) / (rzf[vr] - rzf[vvert]);

                    ultima_x[fakedi / 4] = zk * (rxf[vr] - rxf[vvert]) + rxf[vvert];
                    ultima_y[fakedi / 4] = zk * (ryf[vr] - ryf[vvert]) + ryf[vvert];
                }

                ultima_z[fakedi / 4] = uneg;
                fakedi += 4;
            }
        }

        vr2 = fakedi / 4;

        if (vr2 < 3) {
            return;
        }
    }

    // These are the perspective projections from 3D to 2D.
    // A small check was included on 6/12/97 of the minimum and
    // maximum graphs. This makes sure that the polygons which
    // fall within the limits of the screen are not checked by
    // the 2D clipping functions. Is it more intelligent? Before
    // now, the minimum-maximum search cycle was at the bottom
    // and it was executed after clipping 2D. What for? Long
    // and complicated: therefore one thing to not always do.
    _8n = (vr2 - 1) * 8;

    int32_t temp_max_x = lbxl;
    int32_t temp_max_y = lbyl;
    int32_t temp_min_x = ubxl;
    int32_t temp_min_y = ubyl;

    for (int32_t i = vr2 - 1; i >= 0; i--) {
        float base = uno / ultima_z[i];

        float xtest = base * ultima_x[i] + x_centro_f;
        video_x0[i] = xtest;
        mp[i * 2]   = floor(xtest + 0.5);
        if (mp[i * 2] > temp_max_x)
            temp_max_x = mp[i * 2];
        if (mp[i * 2] < temp_min_x)
            temp_min_x = mp[i * 2];

        float ytest   = base * ultima_y[i] + y_centro_f;
        video_y0[i]   = ytest;
        mp[i * 2 + 1] = floor(ytest + 0.5);
        if (mp[i * 2 + 1] > temp_max_y)
            temp_max_y = mp[i * 2 + 1];
        if (mp[i * 2 + 1] < temp_min_y)
            temp_min_y = mp[i * 2 + 1];
    }

    max_x = temp_max_x;
    max_y = temp_max_y;
    min_x = temp_min_x;
    min_y = temp_min_y;

    bool oob = false;
    if (temp_max_x >= ubxl) {
        oob   = true;
        max_x = ubx;
    }
    if (temp_max_y >= ubyl) {
        oob   = true;
        max_y = uby;
    }
    if (temp_min_x < lbxl) {
        oob   = true;
        min_x = lbx;
    }
    if (temp_min_y < lbyl) {
        oob   = true;
        min_y = lby;
    }

    uint16_t fakedi = 0;

    if (!oob)
        goto drawb;

    /* Conversion of points outside of the visible area. These points must be
     * resized to be within the visible area to avoid polygons that are too
     * large, which would lead to both buffer overruns and slow rendering.
     * In this section the vertices of the polygon are reworked on a 2d level
     * to retriangulate them and generate only visible polygons. */

    // Top side of the video

    for (vr = 0; vr < vr2; vr++) {
        if (video_y0[vr] >= lbyf) {
            video_y1[fakedi] = video_y0[vr];
            video_x1[fakedi] = video_x0[vr];

            fakedi += 1;
            continue;
        }

        if (vr >= 1) {
            pvert = vr - 1;
        } else {
            pvert = vr2 - 1;
        }

        if ((vr + 1) > (vr2 - 1)) {
            nvert = 0;
        } else {
            nvert = vr + 1;
        }

        if ((video_y0[pvert] < lbyf) && (video_y0[nvert] < lbyf)) {
            continue;
        }

        if ((video_y0[pvert] < lbyf) || (video_y0[nvert] < lbyf)) {
            if (video_y0[pvert] < lbyf) {
                vvert = nvert;
            } else {
                vvert = pvert;
            }

            if (video_y0[vr] == video_y0[vvert]) {
                video_x1[fakedi] = video_x0[vr];
            } else {
                video_x1[fakedi] =
                    ((lbyf - video_y0[vvert]) / (video_y0[vr] - video_y0[vvert])) *
                        (video_x0[vr] - video_x0[vvert]) +
                    video_x0[vvert];
            }

            video_y1[fakedi] = lbyf;

            fakedi += 1;
        } else {
            if (video_y0[vr] == video_y0[pvert]) {
                video_x1[fakedi] = video_x0[vr];
            } else {
                video_x1[fakedi] =
                    (((lbyf - video_y0[pvert]) / (video_y0[vr] - video_y0[pvert])) *
                     (video_x0[vr] - video_x0[pvert])) +
                    video_x0[pvert];
            }

            video_y1[fakedi] = lbyf;

            if (video_y0[vr] == video_y0[nvert]) {
                video_x1[fakedi + 1] = video_x0[vr];
            } else {
                video_x1[fakedi + 1] =
                    ((lbyf - video_y0[nvert]) / (video_y0[vr] - video_y0[nvert])) *
                        (video_x0[vr] - video_x0[nvert]) +
                    video_x0[nvert];
            }

            video_y1[fakedi + 1] = lbyf;

            fakedi += 2;
        }
    }

    vr3 = fakedi;

    if (vr3 < 3) {
        return;
    }

    // Bottom side of the video.

    fakedi = 0;
    for (vr = 0; vr < vr3; vr++) {
        if (video_y1[vr] <= ubyf) {
            video_y2[fakedi] = video_y1[vr];
            video_x2[fakedi] = video_x1[vr];

            fakedi += 1;
            continue;
        }

        if (vr >= 1) {
            pvert = vr - 1;
        } else {
            pvert = vr3 - 1;
        }

        if ((vr + 1) > (vr3 - 1)) {
            nvert = 0;
        } else {
            nvert = vr + 1;
        }

        if ((video_y1[pvert] > ubyf) && (video_y1[nvert] > ubyf)) {
            continue;
        }

        if ((video_y1[pvert] > ubyf) || (video_y1[nvert] > ubyf)) {
            if (video_y1[pvert] > ubyf) {
                vvert = nvert;
            } else {
                vvert = pvert;
            }

            if (video_y1[vr] == video_y1[vvert]) {
                video_x2[fakedi] = video_x1[vr];
            } else {
                video_x2[fakedi] =
                    ((ubyf - video_y1[vvert]) / (video_y1[vr] - video_y1[vvert])) *
                        (video_x1[vr] - video_x1[vvert]) +
                    video_x1[vvert];
            }

            video_y2[fakedi] = ubyf;

            fakedi += 1;
        } else {
            if (video_y1[vr] == video_y1[pvert]) {
                video_x2[fakedi] = video_x1[vr];
            } else {
                video_x2[fakedi] =
                    (((ubyf - video_y1[pvert]) / (video_y1[vr] - video_y1[pvert])) *
                     (video_x1[vr] - video_x1[pvert])) +
                    video_x1[pvert];
            }

            video_y2[fakedi] = ubyf;

            if (video_y1[vr] == video_y1[nvert]) {
                video_x2[fakedi + 1] = video_x1[vr];
            } else {
                video_x2[fakedi + 1] =
                    ((ubyf - video_y1[nvert]) / (video_y1[vr] - video_y1[nvert])) *
                        (video_x1[vr] - video_x1[nvert]) +
                    video_x1[nvert];
            }

            video_y2[fakedi + 1] = ubyf;

            fakedi += 2;
        }
    }
    vr4 = fakedi;

    if (vr4 < 3) {
        return;
    }

    // Left side of the video.
    fakedi = 0;
    for (vr = 0; vr < vr4; vr++) {
        if (video_x2[vr] >= lbxf) {
            video_x3[fakedi] = video_x2[vr];
            video_y3[fakedi] = video_y2[vr];

            fakedi += 1;
            continue;
        }

        if (vr >= 1) {
            pvert = vr - 1;
        } else {
            pvert = vr4 - 1;
        }

        if ((vr + 1) > (vr4 - 1)) {
            nvert = 0;
        } else {
            nvert = vr + 1;
        }

        if ((video_x2[pvert] < lbxf) && (video_x2[nvert] < lbxf)) {
            continue;
        }

        if ((video_x2[pvert] < lbxf) || (video_x2[nvert] < lbxf)) {
            if (video_x2[pvert] < lbxf) {
                vvert = nvert;
            } else {
                vvert = pvert;
            }

            if (video_x2[vr] == video_x2[vvert]) {
                video_y3[fakedi] = video_y2[vr];
            } else {
                video_y3[fakedi] =
                    ((lbxf - video_x2[vvert]) / (video_x2[vr] - video_x2[vvert])) *
                        (video_y2[vr] - video_y2[vvert]) +
                    video_y2[vvert];
            }

            video_x3[fakedi] = lbxf;

            fakedi += 1;
        } else {
            if (video_x2[vr] == video_x2[pvert]) {
                video_y3[fakedi] = video_y2[vr];
            } else {
                video_y3[fakedi] =
                    (((lbxf - video_x2[pvert]) / (video_x2[vr] - video_x2[pvert])) *
                     (video_y2[vr] - video_y2[pvert])) +
                    video_y2[pvert];
            }

            video_x3[fakedi] = lbxf;

            if (video_x2[vr] == video_x2[nvert]) {
                video_y3[fakedi + 1] = video_y2[vr];
            } else {
                video_y3[fakedi + 1] =
                    ((lbxf - video_x2[nvert]) / (video_x2[vr] - video_x2[nvert])) *
                        (video_y2[vr] - video_y2[nvert]) +
                    video_y2[nvert];
            }

            video_x3[fakedi + 1] = lbxf;

            fakedi += 2;
        }
    }
    vr5 = fakedi;

    if (vr5 < 3) {
        return;
    }

    // Right side of the video.
    fakedi = 0;
    for (vr = 0; vr < vr5; vr++) {
        if (video_x3[vr] <= ubxf) {
            mp[fakedi]     = floor(video_x3[vr] + 0.5);
            mp[fakedi + 1] = floor(video_y3[vr] + 0.5);

            fakedi += 2;

            continue;
        }

        if (vr >= 1) {
            pvert = vr - 1;
        } else {
            pvert = vr5 - 1;
        }

        if ((vr + 1) > (vr5 - 1)) {
            nvert = 0;
        } else {
            nvert = vr + 1;
        }

        if ((video_x3[pvert] > ubxf) && (video_x3[nvert] > ubxf)) {
            continue;
        }

        if (video_x3[pvert] > ubxf || video_x3[nvert] > ubxf) {
            if (video_x3[pvert] > ubxf) {
                vvert = nvert;
            } else {
                vvert = pvert;
            }

            if (video_x3[vr] == video_x3[vvert]) {
                mp[fakedi + 1] = floor(video_y3[vr] + 0.5);
            } else {
                mp[fakedi + 1] = floor(
                    ((ubxf - video_x3[vvert]) / (video_x3[vr] - video_x3[vvert])) *
                        (video_y3[vr] - video_y3[vvert]) +
                    video_y3[vvert] + 0.5);
            }

            mp[fakedi] = ubx & 0x0000FFFF; // y tho

            fakedi += 2;
            continue;
        } else {
            if (video_x3[vr] == video_x3[pvert]) {
                mp[fakedi + 1] = floor(video_y3[vr] + 0.5);
            } else {
                mp[fakedi + 1] = floor(
                    ((ubxf - video_x3[pvert]) / (video_x3[vr] - video_x3[pvert])) *
                        (video_y3[vr] - video_y3[pvert]) +
                    video_y3[pvert] + 0.5);
            }

            mp[fakedi] = ubx & 0x0000FFFF;

            if (video_x3[vr] == video_x3[nvert]) {
                mp[fakedi + 3] = floor(video_y3[vr] + 0.5);
            } else {
                mp[fakedi + 3] = floor(
                    ((ubxf - video_x3[nvert]) / (video_x3[vr] - video_x3[nvert])) *
                        (video_y3[vr] - video_y3[nvert]) +
                    video_y3[nvert] + 0.5);
            }

            mp[fakedi + 2] = ubx & 0x0000FFFF;

            fakedi += 4;
            continue;
        }
    }

    _8n = (fakedi - 2) * 4;
    vr6 = fakedi;

    if (vr6 < 3) {
        return;
    }

/* Filling of the resulting polygon. */
// Quick tracing (3 ballets (Translation error..?) & away, for small polys).
drawb:

    if (!flares) {
        if (min_y == max_y) {
            if (min_x == max_x) {
                adapted[min_x + riga[min_y]] = colore;
            } else {
                ptr = max_x + riga[min_y];

                while (max_x >= min_x) {
                    adapted[ptr] = colore;
                    max_x--;
                    ptr--;
                }
            }

            return;
        }
    }

    // Complete tracing.
    // Draw the edges of the polygon, with the segment function.
    for (fakedi = 0; fakedi < (_8n / 4); fakedi += 2) {
        xp = mp[fakedi];
        yp = mp[fakedi + 1];
        xa = mp[fakedi + 2];
        ya = mp[fakedi + 3];

        Segmento();
    }

    xp = mp[fakedi];
    yp = mp[fakedi + 1];
    xa = mp[0];
    ya = mp[1];

    Segmento();
    // Starting Pixels
    uint16_t segmptr = riga[min_y] + min_x;
    // Arrival pixels
    uint16_t lim_y = riga[max_y] + min_x;
    uint16_t lim_x = segmptr + max_x - min_x;
    uint16_t bytes = lim_x - segmptr + 2;

    uint16_t loc0, loc1, presip, yeet, tempBytes, tinkywinky, laalaa;
    uint32_t tempfakedi;
    uint8_t dipsy, po;

    switch (flares) {
    case 0:
        for (fakedi = segmptr; fakedi <= lim_y; fakedi += 320) {
            tempBytes  = bytes;
            tempfakedi = fakedi;

            if (adapted[tempfakedi] != 255) {
                while (--tempBytes > 0 && adapted[++tempfakedi] != 255)
                    ;
            }

            if (tempBytes == 0)
                continue;
            loc0 = tempfakedi;

            while (tempBytes-- > 0 && adapted[tempfakedi++] == 255)
                ;
            loc1 = tempfakedi;

            if (adapted[tempfakedi] != 255 && tempBytes > 0) {
                while (--tempBytes > 0 && adapted[++tempfakedi] != 255)
                    ;
            }

            if (tempBytes > 0) {
                while (tempBytes-- > 0 && adapted[tempfakedi++] == 255)
                    ;

                tempfakedi--;
                tempfakedi = tempfakedi > 64000 ? 64000 : tempfakedi;
                memset(&adapted[loc0], colore, tempfakedi - loc0);
            } else {
                loc1--;
                memset(&adapted[loc0], colore, loc1 - loc0);
            }
        }
        break;

    case 1:
        colore &= 0x3F;
        for (fakedi = segmptr; fakedi <= lim_y; fakedi += 320) {
            tempBytes  = bytes;
            tempfakedi = fakedi;

            if (adapted[tempfakedi] != 255) {
                while (--tempBytes > 0 && adapted[++tempfakedi] != 255)
                    ;
            }

            if (tempBytes == 0)
                continue;
            loc0 = tempfakedi;

            while (tempBytes-- > 0 && adapted[tempfakedi++] == 255)
                ;
            loc1 = tempfakedi;

            if (adapted[tempfakedi] != 255 && tempBytes > 0) {
                while (--tempBytes > 0 && adapted[++tempfakedi] != 255)
                    ;
            }

            if (tempBytes > 0) {
                while (tempBytes-- > 0 && adapted[tempfakedi++] == 255)
                    ;
                tempfakedi--;

                tinkywinky = tempfakedi - loc0;
                tempfakedi = loc0;

                for (; tinkywinky > 0; tempfakedi++, tinkywinky--) {
                    dipsy = adapted[tempfakedi - 1];
                    dipsy &= 0x3F;
                    dipsy += colore;

                    if (dipsy >= 62)
                        dipsy = 62;

                    adapted[tempfakedi] = dipsy;
                }
            } else {
                loc1--;

                tinkywinky = loc1 - loc0;
                tempfakedi = loc0;

                dipsy = colore >> 1;

                for (; tinkywinky > 0; tempfakedi++, tinkywinky--) {
                    po = adapted[tempfakedi - 1];
                    po &= 0x3F;
                    po += dipsy;

                    if (po >= 62)
                        po = 62;

                    adapted[tempfakedi] = dipsy;
                }
            }
        }
        break;
    case 2:
        for (fakedi = segmptr; fakedi <= lim_y; fakedi += 320) {
            tempBytes  = bytes;
            tempfakedi = fakedi;

            if (adapted[tempfakedi] != 255) {
                while (--tempBytes > 0 && adapted[++tempfakedi] != 255)
                    ;
            }

            if (tempBytes == 0)
                continue;
            loc0 = tempfakedi;

            while (tempBytes-- > 0 && adapted[tempfakedi++] == 255)
                ;
            loc1 = tempfakedi;

            if (adapted[tempfakedi] != 255 && tempBytes > 0) {
                while (--tempBytes > 0 && adapted[++tempfakedi] != 255)
                    ;
            }

            if (tempBytes > 0) {
                while (tempBytes-- > 0 && adapted[tempfakedi++] == 255)
                    ;
                tempfakedi--;

                tinkywinky = tempfakedi - loc0;
                tempfakedi = loc0;

                for (; tinkywinky > 0; tempfakedi++, tinkywinky--) {
                    tempfakedi = tempfakedi > 64000 ? 64000 : tempfakedi;
                    if (adapted[tempfakedi] == 0xFF) {
                        dipsy = adapted[tempfakedi - 321];
                        dipsy &= 0x3F;
                        dipsy |= 0x40;
                        adapted[tempfakedi] = dipsy;
                    } else {
                        laalaa = adapted[tempfakedi];
                        laalaa &= 0x3F;
                        laalaa |= 0x40;
                        laalaa += tinkywinky;
                        if (laalaa >= 128)
                            laalaa = 127;
                        adapted[tempfakedi] = laalaa;
                    }
                }
            } else {
                loc1--;
                for (tempfakedi = loc0; tempfakedi < loc1; tempfakedi++) {
                    dipsy = adapted[tempfakedi - 321];
                    if (dipsy == 0xFF) {
                        dipsy = adapted[tempfakedi - 642];
                    }

                    dipsy &= 0x3F;
                    dipsy |= 0x40;
                    adapted[tempfakedi] = dipsy;
                }
            }
        }
        break;
        // effetto flares = 3 spostato a "polymap"
    case 4:
        #if 0
        asm {   pusha
                les di, dword ptr adapted
                add lim_y, di
                add di, segmptr }
        fil4a:
        asm {   mov al, colore
                mov ah, colore
                db 0x66 // macro: shl eax, 16
                db 0xc1
                db 0xe0
                db 0x10
                mov ah, colore
                push di
                mov cx, bytes
                mov al, 255
                repne scasb
                jne fil4d
                mov si, di
                repe scasb
                mov bx, di
                repne scasb
                jne fil4e
                repe scasb
                dec di
                dec si
                mov al, colore
                mov dx, di
                sub dx, si
                mov di, si
                mov cx, dx
                shr cx, 2
                jz fil4c }
        fil4b:
        asm {   db 0x26, 0x66, 0x89, 0x05 // mov es:[di], eax
                add di, 4
                dec cx
                jnz fil4b }
        fil4c:
        asm {   mov cl, dl
                and cl, 3
                rep stosb
                jmp fil4d }
        fil4e:
        asm {   dec si
                dec bx
                mov cx, bx
                mov al, colore
                sub cx, si
                mov di, si
                rep stosb }
        fil4d:
        asm {   mov al, colore
                mov ah, colore
                and al, 0x3F
                and ah, 0xC0
                add al, ent
                cmp al, 0x3F
                jbe fil4f
                mov al, 0x3F
                test ent, 0x80
                jz  fil4f
                xor al, al }
        fil4f:
        asm {   or  al, ah
                mov colore, al
                pop di
                add di, 320
                cmp di, lim_y
                jbe fil4a
                popa }*/
        #endif
        STUB
        break;
    }
}

/*

    Sezione per il texture mapping.

*/

#define OK 1     // Ritorno positivo.
#define NOT_OK 0 // Ritorno negativo.

uint8_t *txtr; /* Area della texture (FLS a livelli di intensit�,
                 64 livelli per pixel, senza header).*/

int8_t init_texture_mapping() {
    txtr = (uint8_t*) malloc(((int32_t) TEXTURE_YSIZE) * 256 + 16);
    if (txtr) {
        return (OK);
    } else {
        return (NOT_OK);
    }
}


int8_t load_texture(int8_t *fname, int32_t offset)
/*  Load a bitmap:
    FLS format at luminance levels,
    64 levels per pixel, without header. */
{
    int16_t fh, sl;
    uint32_t p;
    fh = open ((char*) fname, 0);

    if (fh > -1) {
        if (offset >= 0) {
            lseek (fh, offset, SEEK_SET);
        } else {
            lseek (fh, offset, SEEK_END);
        }

        sl = 0;
        p  = 12;

        while (sl < TEXTURE_YSIZE) {
            uint8_t result = read (fh, &txtr[p], TEXTURE_XSIZE);
            if (result == 0) break;
            p += 256;
            sl++;
        }

        close(fh);

        if (sl == TEXTURE_YSIZE) {
            return (OK);
        } else {
            return (NOT_OK);
        }
    } else {
        return (NOT_OK);
    }
}

int8_t fast_load_texture(int8_t *fname) { /* Only for 256 x 256 bitmaps. */
    int16_t fh;
    uint32_t p;
    fh = open((char*) fname, 0);

    if (fh > -1) {
        p  = 12;
        read (fh, &txtr[p], 32768);
        p += 32768;
        read (fh, &txtr[p], 32768);
        close (fh);
        return (OK);
    } else {
        return (NOT_OK);
    }
}

/*  Variabili e funzioni per i procedimenti di shading.
    Possono essere cambiate per conferire un aspetto diverso
    ad ogni poligono.

    int8_t    shade = 0;        // in che modo i poligoni intercettano la luce

    #define NORMAL       0x00 // luminosit� non curata

              /* sezione spotlight */
#define SPOTLIGHT 0x10         // torcia elettrica o faro, con centro su <xl, yl>
int16_t xl         = x_centro; // coordinate video del centro di luce
int16_t yl         = y_centro; // coordinata y
int16_t aspect     = 8;        // aspetto d. proiezione: + alto = + schiacciato
int16_t fallout    = 20;       // evidenza della zona centrale: + alto = + diffuso
int16_t lt_range   = 30;       // ampiezza zona illumin.: 0 = infinita
int16_t absorption = 45;       // assorbimento della superficie, da 0 a 63

/* sezione radiosity semplificata */
#define RADIOSITY 0x20 // luminosit� regolabile orizzontalmente o vertic.
int8_t shadymask  = 1;
int16_t shady_aux = 0;

int16_t xsh, ysh, ksh; // variabili di lavoro che devono rientrare in DS.

float pnx, pny, pnz; // valori di ritorno della funzione successiva. */

void pnorm(float *x, float *y, float *z)
// Calcola i coefficenti x/y/z della normale ad un poligono.
// Valori calcolati in pnx, pny, pnz.
// Richiesti almeno tre vertici.
// N.B. I vertici sono scanditi in senso orario,
//      il vettore risultante rispetta la regola della mano destra.
{
    float x1, y1, z1;
    float x2, y2, z2;
    float xr, yr, zr;
    float ln;
    x1 = x[0] - x[2];
    y1 = y[0] - y[2];
    z1 = z[0] - z[2];
    x2 = x[1] - x[2];
    y2 = y[1] - y[2];
    z2 = z[1] - z[2];
    xr = y1 * z2 - y2 * z1;
    yr = z1 * x2 - z2 * x1;
    zr = x1 * y2 - x2 * y1;
    ln = sqrt(xr * xr + yr * yr + zr * zr);

    if (ln > 0) {
        ln  = 1 / ln;
        pnx = xr * ln;
        pny = yr * ln;
        pnz = zr * ln;
    } else {
        pnx = 0;
        pny = 0;
        pnz = 0;
    }
}

/*  Se si desidera una texture sui poligoni, invocare quest'altra funzione.
    Il parametro "tinta" permette di specificare il colore meno luminoso
    di una sequenza di sue copie pi� intense: il numero di queste
    gradazioni non � importante per polymap.

    E` leggermente pi� lenta nel riempimento, ma non � grave.
    N.B. Il poligono deve avere sempre 4 vertici, anche se specificare il
    numero di vertici effettivi con il parametro "nv" aggiusta la
    texture in modo che i bordi coincidano.

    P.S. Sulla pagina nascosta, bisogna prevedere un byte in pi� (64001),
    perch� vi viene conservato il numero del primo colore della texture,
    dato che i parametri sullo stack non sono raggiungibili nel ciclo
    di tracciamento principale, per ragioni di ottimizzazione. */

float _0  = 0.0;
float _16 = 16.0;

float x_antialias = 1.125;
float y_antialias = 1.125;
float z_antialias = 1.125;

int8_t culling_needed  = 0; // flag: traccia due punti per volta.
int8_t halfscan_needed = 0; // flag: traccia due linee per volta.

uint8_t escrescenze = 0xE0; // primo colore dei bumps (escrescenze)

void polymap(float* x, float* y, float* z, int8_t nv, uint8_t tinta) {
    float ultima_x[2 * VERTICI_PER_POLIGONO];
    float ultima_y[2 * VERTICI_PER_POLIGONO];
    float ultima_z[2 * VERTICI_PER_POLIGONO];
    int16_t   fpart[MPIY];
    int16_t   ipart[MPIY];
    float tempXsize = XSIZE;
    float tempYsize = YSIZE;
    int8_t  _flares = flares;
    float _uno = 1;
    int8_t  culling = culling_needed;
    int8_t  halfscan = halfscan_needed;
    int16_t   vr22;
    int32_t  bndx;
    int16_t   i, sections;
    int32_t  i_low_lim = -10000;
    int32_t  i_hig_lim = +10000;
    int32_t  u, v, min_y, max_y;
    int32_t  x1, y1, x2, y2, ity, jty, h;
    float k1, k2, k3, k4;
    float hx, vx, ox, hy, vy, oy, hz, vz, oz, _x, _y, _z;
    float kx, rx, ry, rz, mx, my, mz, nx, ny, nz, xx, yy, zz;
    float midx, midy, midz;
    float trxf[4], tryf[4], trzf[4];

    // Polymap is made to draw quads.
    // If drawing a triangle, the last vertex must be duplicated.

    if (nv == 3) {
        x[3] = x[2];
        y[3] = y[2];
        z[3] = z[2];
    }

    // Rototranslations of the vertices; the data still remains 3D.
    doflag = 0;

    for (i = 0; i < 4; i++) {
        xx = x[i] - cam_x;
        yy = y[i] - cam_y;
        zz = z[i] - cam_z;

        z2 = (zz * opt_tcosbeta) - (xx * opt_tsinbeta);

        rxf[i] = (xx * opt_tcosbeta) + (zz * opt_tsinbeta);
        rzf[i] = (yy * opt_tsinalfa) + (z2 * opt_tcosalfa);

        if (gamma == 0) {
            ryf[i] = (yy * opt_tcosalfa) - (z2 * opt_tsinalfa);
        } else {
            // TODO; Untested.
            my = (yy * opt_tcosalfa) - (z2 * opt_tsinalfa);

            ryf[i] = (my * opt_tcosngamma) - (rxf[i] * opt_tsinngamma);
            rxf[i] =  (rxf[i] * opt_tcosngamma) + (my * opt_tsinngamma);
        }

        if (rzf[i] < uneg) {
            rwf[i] = 0;
        } else {
            rwf[i] = 1;
            doflag++;
        }
    }

    if (!doflag) return;

    // Calculation of texture mapping tracking vectors.
    vectors:

    if (nv == 3) {
        midx = (rxf[0] + rxf[1] + rxf[2]) * 0.3333333;
        midy = (ryf[0] + ryf[1] + ryf[2]) * 0.3333333;
        midz = (rzf[0] + rzf[1] + rzf[2]) * 0.3333333;

        for (vr = 0; vr < 3; vr++) {
            trxf[vr] = (rxf[vr] - midx) * x_antialias + midx;
            tryf[vr] = (ryf[vr] - midy) * y_antialias + midy;
            trzf[vr] = (rzf[vr] - midz) * z_antialias + midz;
        }
    } else {
        midx = (rxf[0] + rxf[1] + rxf[2] + rxf[3]) * 0.25;
        midy = (ryf[0] + ryf[1] + ryf[2] + ryf[3]) * 0.25;
        midz = (rzf[0] + rzf[1] + rzf[2] + rzf[3]) * 0.25;

        for (vr = 0; vr < 4; vr++) {
            trxf[vr] = (rxf[vr] - midx) * x_antialias + midx;
            tryf[vr] = (ryf[vr] - midy) * y_antialias + midy;
            trzf[vr] = (rzf[vr] - midz) * z_antialias + midz;
        }
    }

    rx = trxf[0];
    mx = trxf[1] - rx;
    nx = rx - trxf[3];

    ry = tryf[0];
    my = tryf[1] - ry;
    ny = ry - tryf[3];

    rz = trzf[0];
    mz = trzf[1] - rz;
    nz = rz - trzf[3];

    hx = (rx * mz - rz * mx) * YCOEFF;
    hy = (rx * nz - rz * nx) * YCOEFF;
    hz = (nx * mz - nz * mx) / dpp;

    vx = (rz * my - ry * mz) * XCOEFF;
    vy = (rz * ny - ry * nz) * XCOEFF;
    vz = (nz * my - ny * mz) / dpp;

    ox = (ry * mx - rx * my) * EMU_K;
    oy = (ry * nx - rx * ny) * EMU_K;
    oz =  ny * mx - nx * my;

    // Fast-load for fully visible polygons.
    if (doflag == 4) {
        vr2 = 4;
        vr22 = 8;
        for (i = 0; i < 4; i++) {
            ultima_x[i] = rxf[i];
            ultima_y[i] = ryf[i];
            ultima_z[i] = rzf[i];
        }
    } else {
        // Clip points behind the observer compared to the screen plane.
        uint16_t fakedi = 0;
        for (vr = 0; vr < 4; vr++) {
            if (rwf[vr] != 0) {
                ultima_x[fakedi / 4] = rxf[vr];
                ultima_y[fakedi / 4] = ryf[vr];
                ultima_z[fakedi / 4] = rzf[vr];
                fakedi += 4;
                continue;
            }

            if (vr >= 1) {
                pvert = vr - 1;
            } else {
                pvert = 3;
            }

            if ((vr + 1) <= 3) {
                nvert = vr + 1;
            } else {
                nvert = 0;
            }

            if (rwf[pvert] == 0 && rwf[nvert] == 0) continue;

            if (rwf[pvert] + rwf[nvert] == 2) {
                if (rzf[vr] == rzf[pvert]) {
                    ultima_x[fakedi / 4] = rxf[vr];
                    ultima_y[fakedi / 4] = ryf[vr];
                } else {
                    zk = (uneg - rzf[pvert]) / (rzf[vr] - rzf[pvert]);

                    ultima_x[fakedi / 4] = zk * (rxf[vr] - rxf[pvert]) + rxf[pvert];
                    ultima_y[fakedi / 4] = zk * (ryf[vr] - ryf[pvert]) + ryf[pvert];
                }
                ultima_z[fakedi / 4] = uneg;

                if (rzf[vr] == rzf[nvert]) {
                    ultima_x[(fakedi / 4) + 1] = rxf[vr];
                    ultima_y[(fakedi / 4) + 1] = ryf[vr];
                } else {
                    zk = (uneg - rzf[nvert]) / (rzf[vr] - rzf[nvert]);

                    ultima_x[(fakedi / 4) + 1] = zk * (rxf[vr] - rxf[nvert]) + rxf[nvert];
                    ultima_y[(fakedi / 4) + 1] = zk * (ryf[vr] - ryf[nvert]) + ryf[nvert];
                }
                ultima_z[(fakedi / 4) + 1] = uneg;
                fakedi += 8;
            } else {
                if (rwf[pvert] == 0) {
                    vvert = nvert;
                } else {
                    vvert = pvert;
                }

                if (rzf[vvert] == rzf[vr]) {
                    ultima_x[fakedi / 4] = rxf[vr];
                    ultima_y[fakedi / 4] = ryf[vr];
                } else {
                    zk = (uneg - rzf[vvert]) / (rzf[vr] - rzf[vvert]);

                    ultima_x[fakedi / 4] = zk * (rxf[vr] - rxf[vvert]) + rxf[vvert];
                    ultima_y[fakedi / 4] = zk * (ryf[vr] - ryf[vvert]) + ryf[vvert];
                }

                ultima_z[fakedi / 4] = uneg;
                fakedi += 4;
            }
        }

        vr2 = fakedi / 4;
        vr22 = fakedi / 2;

        if (vr2 < 3) {
            return;
        }
    }

    // These are the perspective projections from 3D to 2D.
    // The cycle simultaneously searches for the min and max vertical
    // polygon extension on the screen.
    int32_t temp_min_y = ubyl;
    int32_t temp_max_y = lbyl;

    for (i = (vr2 - 1); i >= 0 ; i--) {
        float base = dpp / ultima_z[i];

        float xtest = base * ultima_x[i] + x_centro_f;
        float ytest = base * ultima_y[i] + y_centro_f;

        mp[i * 2] = floor(xtest + 0.5);
        mp[i * 2 + 1] = floor(ytest + 0.5);

        temp_min_y = (mp[i * 2 + 1] < temp_min_y) ? mp[i * 2 + 1] : temp_min_y;
        temp_max_y = (mp[i * 2 + 1] > temp_max_y) ? mp[i * 2 + 1] : temp_max_y;
    }

    min_y = temp_min_y;
    max_y = temp_max_y;

    // Possible exclusion of non-visible polygons.
    if (min_y > uby || max_y < lby) return;

    // Normalization of limits.
    min_y = (min_y < lby) ? lby : min_y;
    max_y = (max_y > uby) ? uby : max_y;

    if (min_y > max_y) return;

    // Initialization of optimization constants.
    ct2ok:
    k1 = vx * 16;
    k2 = vy * 16;
    k3 = vz * 16;

    if (culling) {
        k1 *= 2;
        k2 *= 2;
        k3 *= 2;
    }

    // Initialization of the horizontal border matrices.
    for (i = min_y; i <= max_y; i++) {
        fpart[i] = lbxl;
        ipart[i] = ubxl;
    }

    // Calculation of horizontal borders.
    mp[vr22] = mp[0];
    mp[vr22 + 1] = mp[1];

    for (i = 0; i < vr22; i+= 2) {
        if (mp[i + 3] < mp[i + 1]) {
            x1 = mp[i + 2];
            x2 = mp[i];

            y1 = mp[i + 3];
            y2 = mp[i + 1];
        } else {
            x1 = mp[i];
            x2 = mp[i + 2];

            y1 = mp[i + 1];
            y2 = mp[i + 3];
        }

        if (y1 != y2) {
            kx = ((float) (x2 - x1)) / ((float) (y2 - y1));

            if (y1 < lbyl) {
                ity = lbyl;

                x1 += floor(((float) (lbyl - y1)) * kx + 0.5);
            } else {
                ity = y1;
            }

            if (y2 > ubyl) {
                jty = ubyl;
            } else {
                jty = y2;
            }

            if (ity < jty) {
                float tinkywinky = x1;
                for (h = ity; h <= jty; h++) {
                    bndx = tinkywinky;

                    if (bndx < i_low_lim) bndx = i_low_lim;
                    if (bndx > i_hig_lim) bndx = i_hig_lim;

                    if (bndx > fpart[h] && bndx < ubxl) {
                        fpart[h] = bndx;
                    } else if (bndx >= ubxl) {
                        fpart[h] = ubxl;
                    }

                    if (bndx < ipart[h] && bndx > lbxl) {
                        ipart[h] = bndx;
                    } else if (bndx <= lbxl) {
                        ipart[h] = lbxl;
                    }

                    tinkywinky += kx;
                }
            }
        }
    }

    // Pre-work assignments.
    float fu, fv;
    int32_t tempu, tempv;
    uint16_t tax, tbx, tdx, tbp, tds, fakedi, fakesi, tempfakedi;
    uint8_t tcl, tch, tbl, tbh, tah, tal, tdh, tdl, tempch;
    adapted[0xFA00] = tinta;
    adapted[0xFA01] = escrescenze;

    //asm les dx, dword ptr txtr      // Loading the texture segment.
    //asm mov ax, es                  // .
    //asm shr dx, 4                   // .
    //asm add ax, dx                  // .
    //asm db 0x8e, 0xe0               // mov fs, ax
    //asm les ax, dword ptr adapted   // Loading video area address.

    // Tracking cycle. (NOTE: This makes no sense.)
    for (i = min_y; i <= max_y;) {
        _x = ox + (hx * (i - y_centro_f)) + (vx * (ipart[i] - x_centro_f + 1));
        _y = oy + (hy * (i - y_centro_f)) + (vy * (ipart[i] - x_centro_f + 1));
        _z = oz + (hz * (i - y_centro_f)) + (vz * (ipart[i] - x_centro_f + 1));

        k4 = 1.0 / _z;

        u = floor((_x * tempXsize) * k4 + 0.5);
        v = floor((_y * tempYsize) * k4 + 0.5);

        sections = fpart[i] - ipart[i];
        fakedi = i * 320 + ipart[i] - 4; // NOTE: Fudge factor to account for loss of offset on adapted.

        if (culling) {
            goto c_row;
        }

        //  High detail scanline tracing.
        row:
        if (sections <= 0) {
            goto row_end;
        }

        again:
        if (sections > 16) {
            tcl = 16;
        } else {
            tax = sections;
            tah = (tax >> 8);
            tal = (tax & 0xFF);
            tcl = tal;
        }

        complete:
        tcl = 16;

        unfinished:
        sections -= 16;
        if (tcl == 0) {
            goto row;
        }
        _x += k1;
        _y += k2;
        _z += k3;

        k4 = 1 / _z;

        tempu = u;
        tempv = v;

        fu = (_x * tempXsize) * k4;
        fv = (_y * tempYsize) * k4;

        u = (fu - floor(fu) > 0.5) ? ceil(fu) : floor(fu);
        v = (fv - floor(fv) > 0.5) ? ceil(fv) : floor(fv);

        tax = tempu;
        tdx = tempv;
        fakesi = (v - tempv) >> 4;
        tbp = (u - tempu) >> 4;

        tch = _flares;
        if (tch & 1) {
            goto transp;
        } else if (tch & 2) {
            goto bright;
        } else if (tch & 4) {
            goto merger;
        } else if (tch & 8) {
            goto bumper;
        } else {
            goto internal;
        }

        internal:
        tdh = (tdx >> 8) & 0xFF;
        tah = (tax >> 8) & 0xFF;

        tbh = tdh;
        fakedi++;
        tbl = tah;
        tch = adapted[0xFA00];
        tbx = (((uint16_t) tbh) << 8) + tbl;
        tch += txtr[tbx - 4]; // NOTE; Fudge factor to account for loss of offset on txtr.
        tax += tbp;
        adapted[fakedi + 3] = tch;
        tdx += fakesi;
        tcl--;
        if (tcl != 0) goto internal;
        goto common;

        transp:
        tdh = (tdx >> 8) & 0xFF;
        tah = (tax >> 8) & 0xFF;

        tbh = tdh;
        fakedi++;
        tbl = tah;
        tch = adapted[fakedi + 3];
        tbx = (((uint16_t) tbh) << 8) + tbl;
        tch += txtr[tbx - 4];
        tax += tbp;
        adapted[fakedi + 3] = tch;
        tdx += fakesi;
        tcl--;
        if (tcl != 0) goto transp;
        goto common;

        bright: // NOTE: This is for the text rendering.
        tdh = (tdx >> 8) & 0xFF;
        tah = (tax >> 8) & 0xFF;

        tch = adapted[fakedi + 4];
        tbh = tdh;
        fakedi++;
        tbl = tah;
        tch &= 0x3F;
        tax += tbp;
        tbx = (((uint16_t) tbh) << 8) + tbl;
        tch += txtr[tbx - 4];
        tdx += fakesi;
        if (tch <= 0x3E) goto antibloom;
        tch = 0x3E;

        antibloom:
        adapted[fakedi + 3] &= 0xC0;
        adapted[fakedi + 3] |= tch;
        tcl--;
        if (tcl != 0) goto bright;
        goto common;

        merger:
        tdh = (tdx >> 8) & 0xFF;
        tah = (tax >> 8) & 0xFF;

        tch = adapted[fakedi + 4];
        tbh = tdh;
        fakedi++;
        tbl = tah;
        tch &= 0x3F;
        tax += tbp;
        tbx = (((uint16_t) tbh) << 8) + tbl;
        tch += txtr[tbx - 4];
        tch += adapted[0xFA00];
        tdx += fakesi;
        tch >>= 1;
        adapted[fakedi + 3] &= 0xC0;
        adapted[fakedi + 3] |= tch;
        tcl--;
        if (tcl != 0) goto merger;
        goto common;

        bumper:
        tdh = (tdx >> 8) & 0xFF;
        tah = (tax >> 8) & 0xFF;

        tbh = tdh;
        fakedi++;
        tbl = tah;
        tch = adapted[0xFA00];
        tbx = (((uint16_t) tbh) << 8) + tbl;
        tch += txtr[tbx - 4];
        tax += tbp;
        adapted[fakedi + 3] = tch;
        tempfakedi = fakedi;
        tempch = tch;
        tch &= 0x07;

        bmpm320:
        fakedi -= 320;
        tch--;
        if (!((tch >> 7) & 1)) goto bmpm320;
        tch = tempch;
        tch -= adapted[0xFA00];
        tch += adapted[0xFA01];
        tch += txtr[tbx - 4];
        adapted[fakedi + 640 + 3] = tch;
        fakedi = tempfakedi;
        tdx += fakesi;
        tcl--;
        if (tcl != 0) goto bumper;

        common:
        goto row;

        // Low detail scanline tracing.
        c_row:
        #if 0
        asm {
            cmp sections, 0
            jg  c_again
            jmp row_end
        }

        c_again:
        asm {
            cmp sections, 32
            jg  c_complete
            mov ax, sections
            add ax, 2
            mov cl, al
            jmp c_unfinished
        }

        c_complete:
        asm     mov cl, 32

        c_unfinished:
        asm {
            sub sections, 32
            cmp cl, 2
            jl  c_row
            fld   _z           // 1 cycle   stack: z
            fadd  k3               // 1 cycle   stack: z+k3
            fld   _x           // 1 cycle   stack: x, z+k3
            fadd  k1               // 1 cycle   stack: x+k1, z+k3
            fxch                   // no time   stack: z+k3, x+k1
            fst   _z               // 2 cycles  stack: z+k3, x+k1
            fxch                   // no time   stack: x+k1, z+k3
            fst   _x               // 2 cycles  stack: x+k1, z+k3
            fxch                   // no time   stack: z+k3, x+k1
            fdivr _uno             // 39 cycles stack: k4, x+k1
            db 0x66;
            mov ax, word ptr u     // while FPU is working,
            db 0x66;
            mov dx, word ptr v     // this group takes 0 cycles.
            shr cl, 1          //
            push bp                //
            mov ch, _flares        //
            push di            //
            fxch                   // no time   stack: x+k1, k4
            fmul  tempXsize        // 1 cycle   stack: x..., k4
            fld   _y               // 1 cycle   stack: y, x..., k4
            fadd  k2           // 1 cycle   stack: y+k2, x..., k4
            fst   _y               // 3 cycles  stack: y+k2, x..., k4
            fmul  tempYsize        // 1 cycle   stack: y..., x..., k4
            fxch                   // no time   stack: x..., y..., k4
            fmul  st, st(2)        // 1 cycle   stack: u, y..., k4
            fxch                   // no time   stack: y..., u, k4
            fmul  st, st(2)        // 1 cycle   stack: v, u, k4
            fxch                   // no time   stack: u, v, k4
            fistp u                // 6 cycles  stack: v, k4
            fistp v                // 6 cycles  stack: k4
            fstp st
            db 0x66;
            mov si, word ptr v
            db 0x66;
            mov bp, word ptr u
            db 0x66;
            sub si, dx
            db 0x66;
            sub bp, ax
            db 0x66;
            sar si, 4
            db 0x66;
            sar bp, 4
            test ch, 15         // NORMALE = 0
            jz c_internal
            test ch, 1      // LUCIDO = 1
            jnz c_transp
            test ch, 2      // BRILLANTE = 2
            jnz c_bright
            test ch, 4      // TRASLUCIDO = 4
            jnz c_merger
            jmp c_bumper    // BUMP MAPPING = 8
        }

        c_internal:
        asm {
            mov bh, dh      // 1 (riempimento normale)
            add di, 2       // *
            mov bl, ah      // 1
            mov ch, ds:[0xFA00] // *
            db 0x64, 0x02, 0x2F     // 1+PFX+AGI (add ch, fs:[bx])
            add ax, bp      // *
            mov [di+2], ch      // 1
            mov [di+3], ch      // *
            add dx, si      // 1
            dec cl          // *
            jnz c_internal      // 1 (8 cicli).
            jmp c_common
        }

        c_transp:
        asm {
            mov bh, dh      // 1 (riempimento lucido)
            add di, 2       // *
            mov bl, ah      // 1
            mov ch, [di+3]      // *
            db 0x64, 0x02, 0x2F     // 1+PFX+AGI (add ch, fs:[bx])
            add ax, bp      // *
            mov [di+2], ch      // 1
            mov [di+3], ch      // *
            add dx, si      // 1
            dec cl          // *
            jnz c_transp        // 1 (8 cicli).
            jmp c_common
        }

        c_bright:
        asm {
            mov ch, [di+4]      // 1 (riempimento brillante)
            mov bh, dh      // *
            add di, 2       // 1
            mov bl, ah      // *
            and ch, 0x3F        // 1
            add ax, bp      // *
            db 0x64, 0x02, 0x2F     // 1+PFX (add ch, fs:[bx])
            add dx, si      // *
            cmp ch, 0x3E        // 1
            jbe c_antibloom     // * (antiblooming)
            mov ch, 0x3E        // 1
        }

        c_antibloom:
        asm {
            and byte ptr [di+2], 0xC0 // 1
            or  ch, [di+2]      // 1
            dec cl          // 1
            mov [di+2], ch      // *
            mov [di+3], ch      // 1
            jnz c_bright        // 0 (11 cicli.)
            jmp c_common
        }

        c_merger:
        asm {
            mov ch, [di+4]      // 1 (traslucido)
            mov bh, dh      // *
            add di, 2       // 1
            mov bl, ah      // *
            and ch, 0x3F        // 1
            add ax, bp      // *
            db 0x64, 0x02, 0x2F     // 1+PFX (add ch, fs:[bx])
            add ch, ds:[0xFA00]
            add dx, si      // *
            shr ch, 1       // 1
            and byte ptr [di+2], 0xC0 // *
            or  ch, [di+2]      // 1
            dec cl          // *
            mov [di+2], ch      // 1
            mov [di+3], ch      // *
            jnz c_merger        // 1 (9 cicli.)
            jmp c_common
        }

        c_bumper:
        asm {
            mov bh, dh      // 1 (bump mapping)
            add di, 2       // *
            mov bl, ah      // 1
            mov ch, ds:[0xFA00] // *
            db 0x64, 0x02, 0x2F     // 1+PFX+AGI (add ch, fs:[bx])
            add ax, bp      // *
            mov [di+2], ch      // 1
            mov [di+3], ch      // 1
            push di
            push cx
            and ch, 0x7
        }

        c_bmpm320:
        asm {
            sub di, 320
            dec ch
            jns c_bmpm320
            pop cx
            sub ch, ds:[0xFA00]
            add ch, ds:[0xFA01] // *
            db 0x64, 0x02, 0x2F     // 1+PFX+AGI (add ch, fs:[bx])
            mov byte ptr [di+640+2], ch
            mov byte ptr [di+640+3], ch
            pop di
            add dx, si      // *
            dec cl          // 1
            jnz c_bumper    // ? cicli.
        }

        c_common:
        asm {
            pop di
            pop bp
            add di, 32
            jmp c_row
        }
        #endif
        STUB

        // Inter-scanline code: between one scanline and the next.
        row_end:
        if (!(halfscan & 0x01)) goto do_singlescan;
        i++;
        if (i > max_y) return;
        tdx = ipart[i - 2];
        tax = fpart[i - 2];
        fakedi = riga[i];
        if (tax <= tdx) {
            tax -= tdx;
            goto do_singlescan;
        }
        tax -= tdx;
        fakedi += tdx;

        duplicate:
        tdl = adapted[fakedi - 316];
        adapted[fakedi + 4] = tdl;
        adapted[fakedi + 324] = tdl;
        fakedi++;
        tax--;
        if (tax != 0) goto duplicate;

        do_singlescan:
        i++;
        tax = max_y;
    }
}

void Forward(float delta) /* Provoca l'avanzamento dell'osservatore
                 nella direzione di volo di <delta>
                 unit� virtuali. */
{
    cam_x -= delta * opt_tsinbeta * opt_tcosalfa;
    cam_z += delta * opt_tcosbeta * opt_tcosalfa;
    cam_y += delta * opt_tsinalfa;
}

int32_t _x_, _y_;

int8_t getcoords(float x, float y, float z) {
#if 0
    // calcola le coordinate sullo schermo di un punto nello spazio,
    // usando lo stesso nucleo di calcolo di poly3d e di polymap,
    // se il punto rientra nello schermo ritorna 1, altrimenti ritorna 0.
    // le coordinate sono poi trasferite nelle variabili _x_ e _y_.
    // il punto non andrebbe tracciato se non � visibile, perch�
    // le coordinate risulterebbero, in tal caso, indeterminabili.
    float rx, ry, rz, my;
    asm {// if (ngamma != 0) goto t_axis;
        db 0x66;
        mov ax, word ptr _0
        db 0x66;
        cmp ax, word ptr ngamma
        jne t_axis
        jmp no_t_axis }
t_axis:
    asm {   fld z
            fsub cam_z
            fst zz
            fmul opt_tsinbeta
            fld x
            fsub cam_x
            fst xx
            fmul opt_tcosbeta
            faddp
            fstp rx
            fld zz
            fmul opt_tcosbeta
            fld xx
            fmul opt_tsinbeta
            fsubp
            fst z2
            fmul opt_tcosalfa
            fld y
            fsub cam_y
            fst yy
            fmul opt_tsinalfa
            faddp
            fst rz
            fcomp uneg
            fstsw ax
            fld yy
            fmul opt_tcosalfa
            fld z2
            fmul opt_tsinalfa
            fsubp
            fst my
            fmul opt_tcosngamma
            fld rx
            fmul opt_tsinngamma
            fsubp
            fstp ry
            fld my
            fmul opt_tsinngamma
            fld rx
            fmul opt_tcosngamma
            faddp
            fstp rx
            sahf
            jb _rzf_min_uneg
            jmp convert }
    _rzf_min_uneg:
    return (0);
no_t_axis:
    asm {   fld z
            fsub cam_z
            fst zz
            fmul opt_tsinbeta
            fld x
            fsub cam_x
            fst xx
            fmul opt_tcosbeta
            faddp
            fstp rx
            fld zz
            fmul opt_tcosbeta
            fld xx
            fmul opt_tsinbeta
            fsubp
            fst z2
            fmul opt_tcosalfa
            fld y
            fsub cam_y
            fst yy
            fmul opt_tsinalfa
            faddp
            fst rz
            fcomp uneg
            fstsw ax
            fld yy
            fmul opt_tcosalfa
            fld z2
            fmul opt_tsinalfa
            fsubp
            fstp ry
            sahf
            jb rzf_min_uneg
            jmp convert }
    rzf_min_uneg:
    return (0);
convert: //my  = dpp / rz;
    //_x_ = my * rx + x_centro_f;
    //_y_ = my * ry + y_centro_f;
    asm {
        fld dpp
        fdiv rz
        fld st(0)
        fmul rx
        fadd x_centro_f
        fistp _x_
        fmul ry
        fadd y_centro_f
        fistp _y_
    }

    if (_x_ > lbxl && _x_ < ubxl && _y_ > lbyl && _y_ < ubyl) {
        return (1);
    } else {
        return (0);
    }
#endif
    STUB
}

int8_t facing(float *x, float *y, float *z) {
#if 0
    /*  Controlla se un poligono a una sola faccia � visibile o meno.
        Certo che come procedimento non � poi tanto semplice: va calcolata,
        anche approssimativamente, la normale alla superficie; comunque,
        sempre meglio che calcolare tutto il poligono... */
    float x1, y1, z1, x2, y2, z2, xr, yr, zr;
    asm {   les   bx, dword ptr x
            mov   si, word ptr  y
            mov   di, word ptr  z
            fld   dword ptr es:[bx]
            fsub  dword ptr es:[bx+8]
            fstp  x1
            fld   dword ptr es:[si]
            fsub  dword ptr es:[si+8]
            fstp  y1
            fld   dword ptr es:[di]
            fsub  dword ptr es:[di+8]
            fstp  z1
            fld   dword ptr es:[bx+4]
            fsub  dword ptr es:[bx+8]
            fstp  x2
            fld   dword ptr es:[si+4]
            fsub  dword ptr es:[si+8]
            fstp  y2
            fld   dword ptr es:[di+4]
            fsub  dword ptr es:[di+8]
            fst   z2
            fmul  y1
            fld   y2
            fmul  z1
            fsubp
            fstp  xr
            fld   z1
            fmul  x2
            fld   z2
            fmul  x1
            fsubp
            fstp  yr
            fld   x1
            fmul  y2
            fld   x2
            fmul  y1
            fsubp
            fstp  zr
            fld   cam_x
            fsub  dword ptr es:[bx+8]
            fmul  xr
            fld   cam_y
            fsub  dword ptr es:[si+8]
            fmul  yr
            faddp
            fld   cam_z
            fsub  dword ptr es:[di+8]
            fmul  zr
            faddp
            ftst
            ffree st(0)
            fstsw ax
            xor dl, dl
            sahf
            jb _zero
            not dl }
    _zero:
    return (_DL);
#endif
}
