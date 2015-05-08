/* Copyright 1992-2009	Regents of University of California
 *						Lawrence Berkeley National Laboratory
 *
 *  Authors: R.J. Hitchcock and W.L. Carroll
 *           Building Technologies Department
 *           Lawrence Berkeley National Laboratory
 */
/**************************************************************
 * C Language Implementation of DOE2.1d and Superlite 3.0
 * Daylighting Algorithms with new Complex Fenestration System
 * analysis algorithms.
 *
 * The original DOE2 daylighting algorithms and implementation
 * in FORTRAN were developed by F.C. Winkelmann at the
 * Lawrence Berkeley National Laboratory.
 *
 * The original Superlite algorithms and implementation in FORTRAN
 * were developed by Michael Modest and Jong-Jin Kim
 * under contract with Lawrence Berkeley National Laboratory.
 **************************************************************/

// This work was supported by the Assistant Secretary for Energy Efficiency 
// and Renewable Energy, Office of Building Technologies, 
// Building Systems and Materials Division of the 
// U.S. Department of Energy under Contract No. DE-AC03-76SF00098.

/*
NOTICE: The Government is granted for itself and others acting on its behalf 
a paid-up, nonexclusive, irrevocable worldwide license in this data to reproduce, 
prepare derivative works, and perform publicly and display publicly. 
Beginning five (5) years after (date permission to assert copyright was obtained),
subject to two possible five year renewals, the Government is granted for itself 
and others acting on its behalf a paid-up, nonexclusive, irrevocable worldwide
license in this data to reproduce, prepare derivative works, distribute copies to 
the public, perform publicly and display publicly, and to permit others to do so. 
NEITHER THE UNITED STATES NOR THE UNITED STATES DEPARTMENT OF ENERGY, NOR ANY OF
THEIR EMPLOYEES, MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY LEGAL 
LIABILITY OR RESPONSIBILITY FOR THE ACCURACY, COMPLETENESS, OR USEFULNESS OF ANY 
INFORMATION, APPARATUS, PRODUCT, OR PROCESS DISCLOSED, OR REPRESENTS THAT ITS USE 
WOULD NOT INFRINGE PRIVATELY OWNED RIGHTS.
*/

#pragma warning(disable:4786)

// Standard includes
#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace std;

// BGL includes
#include "BGL.h"
namespace BGL = BldgGeomLib;

// includes
#include "const.h"
#include "DBconst.h"
#include "def.h"

// WLC includes
#include "NodeMesh2.h"
#include "WLCSurface.h"
#include "helpers.h"
#include "hemisphiral.h"
#include "btdf.h"
#include "CFSSystem.h"
#include "CFSSurface.h"

// includes
#include "doe2dl.h"
#include "EPlus_LoadData.h"
#include "struct.h"
#include "geom.h"

/****************************** subroutine LoadDataFromEPlus *****************************/
/* Loads DElight Building data from a disk file generated from EnergyPlus data. */
/* Reads surface vertex coordinates in World Coordinate System */
/****************************** subroutine LoadDataFromEPlus *****************************/
int LoadDataFromEPlus(
	BLDG *bldg_ptr,		/* building structure pointer */
	FILE *infile,		/* pointer to building data file */
	ofstream* pofdmpfile)	/* ptr to dump file */
{
	char cInputLine[MAX_CHAR_LINE+1];	/* Input line */
	char *token;						/* Input token pointer */
	char *next_token;					/* Next token pointer for strtok_s call */
	int icoord, im, iz, ils, is, iw, ish, irp, ihr;/* indexes */
	int izshade_x, izshade_y;/* indexes */

	/* Read and discard the second heading line (first line is read in exported function) */
	fgets(cInputLine, MAX_CHAR_LINE, infile);

	/* Read site data */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %s\n",bldg_ptr->name,_countof(bldg_ptr->name));
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->lat);
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->lon);
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->alt);
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->azm);
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->timezone);
	/* monthly atmospheric moisture */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	/* tokenize the line label */
	token = strtok_s(cInputLine," ",&next_token);
	for (im=0; im<MONTHS; im++) {
		token = strtok_s(NULL," ",&next_token);
		bldg_ptr->atmmoi[im] = atof(token);
	}
	/* monthly atmospheric turbidity */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	/* tokenize the line label */
	token = strtok_s(cInputLine," ",&next_token);
	for (im=0; im<MONTHS; im++) {
		token = strtok_s(NULL," ",&next_token);
		bldg_ptr->atmtur[im] = atof(token);
	}

	/* Read and discard ZONES headings lines */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	fgets(cInputLine, MAX_CHAR_LINE, infile);

	/* Read zone data */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->nzones);
	if (bldg_ptr->nzones > MAX_BLDG_ZONES) {
		*pofdmpfile << "ERROR: DElight exceeded maximum ZONES limit of " << MAX_BLDG_ZONES << "\n";
		return(-1);
	}
	for (iz=0; iz<bldg_ptr->nzones; iz++) {
		bldg_ptr->zone[iz] = new ZONE;
		if (bldg_ptr->zone[iz] == NULL) {
			*pofdmpfile << "ERROR: DElight Insufficient memory for ZONE allocation\n";
			return(-1);
		}
		struct_init("ZONE",(char *)bldg_ptr->zone[iz]);

		/* Read and discard ZONE headings lines */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		fgets(cInputLine, MAX_CHAR_LINE, infile);

		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %s\n",bldg_ptr->zone[iz]->name,_countof(bldg_ptr->zone[iz]->name));
		/* zone origin in bldg system coordinates */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		/* tokenize the line label */
		token = strtok_s(cInputLine," ",&next_token);
		for (icoord=0; icoord<NCOORDS; icoord++) {
			token = strtok_s(NULL," ",&next_token);
			bldg_ptr->zone[iz]->origin[icoord] = atof(token);
		}
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->azm);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->mult);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->flarea);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->volume);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->lighting);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->min_power);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->min_light);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->lt_ctrl_steps);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->lt_ctrl_prob);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->view_azm);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->max_grid_node_area);

		/* Read and discard ZONE LIGHTING SCHEDULE headings lines */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		fgets(cInputLine, MAX_CHAR_LINE, infile);

		/* Read lighting schedule data */
		// NOTE: Left here for consistency even though Schedules are not included in EPlus generated input file.
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->nltsch);
		if (bldg_ptr->zone[iz]->nltsch > MAX_LT_SCHEDS) {
			*pofdmpfile << "ERROR: DElight exceeded maximum LIGHTING SCHEDULES limit of " << MAX_LT_SCHEDS << "\n";
			return(-1);
		}
		for (ils=0; ils<bldg_ptr->zone[iz]->nltsch; ils++) {
			bldg_ptr->zone[iz]->ltsch[ils] = new LTSCH;
			if (bldg_ptr->zone[iz]->ltsch[ils] == NULL) {
				*pofdmpfile << "ERROR: DElight Insufficient memory for LIGHT SCHEDULE allocation\n";
				return(-1);
			}
			struct_init("LTSCH",(char *)bldg_ptr->zone[iz]->ltsch[ils]);

			/* Read and discard LIGHTING SCHEDULE DATA headings lines */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);

			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %s\n",bldg_ptr->zone[iz]->ltsch[ils]->name,_countof(bldg_ptr->zone[iz]->ltsch[ils]->name));
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->ltsch[ils]->mon_begin);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->ltsch[ils]->day_begin);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->ltsch[ils]->mon_end);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->ltsch[ils]->day_end);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->ltsch[ils]->dow_begin);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->ltsch[ils]->dow_end);
			/* lighting schedule hourly fractions */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			/* tokenize the line label */
			token = strtok_s(cInputLine," ",&next_token);
			for (ihr=0; ihr<HOURS; ihr++) {
				token = strtok_s(NULL," ",&next_token);
				bldg_ptr->zone[iz]->ltsch[ils]->frac[ihr] = atof(token);
			}
		}

		/* Read and discard ZONE SURFACES headings lines */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		fgets(cInputLine, MAX_CHAR_LINE, infile);

		/* Read surface data */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->nsurfs);
		if (bldg_ptr->zone[iz]->nsurfs > MAX_ZONE_SURFS) {
			*pofdmpfile << "ERROR: DElight exceeded maximum ZONE SURFACES limit of " << MAX_ZONE_SURFS << "\n";
			return(-1);
		}
		for (is=0; is<bldg_ptr->zone[iz]->nsurfs; is++) {
			bldg_ptr->zone[iz]->surf[is] = new SURF;
			if (bldg_ptr->zone[iz]->surf[is] == NULL) {
				*pofdmpfile << "ERROR: DElight Insufficient memory for SURFACE allocation\n";
				return(-1);
			}
			struct_init("SURF",(char *)bldg_ptr->zone[iz]->surf[is]);

			/* Read and discard ZONE SURFACE DATA headings lines */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);

			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %s\n",bldg_ptr->zone[iz]->surf[is]->name,_countof(bldg_ptr->zone[iz]->surf[is]->name));

			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->surf[is]->azm_bs);
			bldg_ptr->zone[iz]->surf[is]->azm_zs = bldg_ptr->zone[iz]->surf[is]->azm_bs - bldg_ptr->zone[iz]->azm;

			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->surf[is]->tilt_bs);
			bldg_ptr->zone[iz]->surf[is]->tilt_zs = bldg_ptr->zone[iz]->surf[is]->tilt_bs;

			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->surf[is]->vis_refl);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->surf[is]->ext_vis_refl);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->surf[is]->gnd_refl);

			/* Vertices in World Coordinate System coordinates */
			int iVertices;
			// Read number of vertices
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&iVertices);
            bldg_ptr->zone[iz]->surf[is]->nvertices = iVertices;
			BGL::point3			p3TmpPt;
			double dTmpVertex[NCOORDS];
			int ivert;
			for (ivert=0; ivert<iVertices; ivert++) {
				// Read the coords of each vertex
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				/* tokenize the line label */
				token = strtok_s(cInputLine," ",&next_token);
				for (icoord=0; icoord<NCOORDS; icoord++) {
					token = strtok_s(NULL," ",&next_token);
					dTmpVertex[icoord] = atof(token);
				}
				// Store the vertex coords as a BGL point3
				p3TmpPt = BGL::point3(dTmpVertex[0], dTmpVertex[1], dTmpVertex[2]);
			    // Store the BGL point3 in the SURF vPt3VerticesWCS_OCCW list
                bldg_ptr->zone[iz]->surf[is]->vPt3VerticesWCS_OCCW.push_back(p3TmpPt);
			}

            // Reorder the set of vertex coords for the WLC vertex list
            // WLC Vertex list begins at inside "lower-left" and is in INSIDE-CounterClockWise order
//BGL::point3 pt3Ddebug = bldg_ptr->zone[iz]->surf[is]->vPt3VerticesWCS_OCCW[2];
            bldg_ptr->zone[iz]->surf[is]->v3List_WLC.push_back(bldg_ptr->zone[iz]->surf[is]->vPt3VerticesWCS_OCCW[2]);
            bldg_ptr->zone[iz]->surf[is]->v3List_WLC.push_back(bldg_ptr->zone[iz]->surf[is]->vPt3VerticesWCS_OCCW[1]);
            bldg_ptr->zone[iz]->surf[is]->v3List_WLC.push_back(bldg_ptr->zone[iz]->surf[is]->vPt3VerticesWCS_OCCW[0]);

            for (ivert=(iVertices-1); ivert>2; ivert--) {
                bldg_ptr->zone[iz]->surf[is]->v3List_WLC.push_back(bldg_ptr->zone[iz]->surf[is]->vPt3VerticesWCS_OCCW[ivert]);
            }

			/* Read and discard SURFACE WINDOWS headings lines */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);

			/* Read window data */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->surf[is]->nwndos);
			if (bldg_ptr->zone[iz]->surf[is]->nwndos > MAX_SURF_WNDOS) {
				*pofdmpfile << "ERROR: DElight exceeded maximum SURFACE WINDOWS limit of " << MAX_SURF_WNDOS << "\n";
				return(-1);
			}
			for (iw=0; iw<bldg_ptr->zone[iz]->surf[is]->nwndos; iw++) {
				bldg_ptr->zone[iz]->surf[is]->wndo[iw] = new WNDO;
				if (bldg_ptr->zone[iz]->surf[is]->wndo[iw] == NULL) {
					*pofdmpfile << "ERROR: DElight Insufficient memory for WINDOW allocation\n";
					return(-1);
				}
				struct_init("WNDO",(char *)bldg_ptr->zone[iz]->surf[is]->wndo[iw]);

				/* Read and discard WINDOW headings lines */
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				fgets(cInputLine, MAX_CHAR_LINE, infile);

				fgets(cInputLine, MAX_CHAR_LINE, infile);
				sscanf_s(cInputLine,"%*s %s\n",bldg_ptr->zone[iz]->surf[is]->wndo[iw]->name,_countof(bldg_ptr->zone[iz]->surf[is]->wndo[iw]->name));
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				sscanf_s(cInputLine,"%*s %s\n",bldg_ptr->zone[iz]->surf[is]->wndo[iw]->glass_type,_countof(bldg_ptr->zone[iz]->surf[is]->wndo[iw]->glass_type));
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->surf[is]->wndo[iw]->shade_flag);
				if (bldg_ptr->zone[iz]->surf[is]->wndo[iw]->shade_flag != 0) {
					fgets(cInputLine, MAX_CHAR_LINE, infile);
					sscanf_s(cInputLine,"%*s %s\n",bldg_ptr->zone[iz]->surf[is]->wndo[iw]->shade_type,_countof(bldg_ptr->zone[iz]->surf[is]->wndo[iw]->shade_type));
				}
				/* window overhang/fin zone shade depth (ft) */
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				/* tokenize the line label */
				token = strtok_s(cInputLine," ",&next_token);
				for (izshade_x=0; izshade_x<NZSHADES; izshade_x++) {
					token = strtok_s(NULL," ",&next_token);
					bldg_ptr->zone[iz]->surf[is]->wndo[iw]->zshade_x[izshade_x] = atof(token);
				}
				/* window overhang/fin zone shade distance from window (ft) */
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				/* tokenize the line label */
				token = strtok_s(cInputLine," ",&next_token);
				for (izshade_y=0; izshade_y<NZSHADES; izshade_y++) {
					token = strtok_s(NULL," ",&next_token);
					bldg_ptr->zone[iz]->surf[is]->wndo[iw]->zshade_y[izshade_y] = atof(token);
				}

				/* Vertices in World Coordinate System coordinates */
				// Read number of vertices
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				sscanf_s(cInputLine,"%*s %d\n",&iVertices);
                bldg_ptr->zone[iz]->surf[is]->wndo[iw]->nvertices = iVertices;
				for (ivert=0; ivert<iVertices; ivert++) {
					// Read the coords of each vertex
					fgets(cInputLine, MAX_CHAR_LINE, infile);
					/* tokenize the line label */
					token = strtok_s(cInputLine," ",&next_token);
					for (icoord=0; icoord<NCOORDS; icoord++) {
						token = strtok_s(NULL," ",&next_token);
						dTmpVertex[icoord] = atof(token);
					}
				    // Store the vertex coords as a BGL point3
				    p3TmpPt = BGL::point3(dTmpVertex[0], dTmpVertex[1], dTmpVertex[2]);
			        // Store the BGL point3 in the WNDO vPt3VerticesWCS_OCCW list
                    // This Vertex list begins at outside "upper-left" and is in OUTSIDE-CounterClockWise order
                    bldg_ptr->zone[iz]->surf[is]->wndo[iw]->vPt3VerticesWCS_OCCW.push_back(p3TmpPt);
				}
				// Reorder the set of vertex coords for the WLC vertex list
                // WLC Vertex list begins at inside "lower-left" and is in INSIDE-CounterClockWise order
                bldg_ptr->zone[iz]->surf[is]->wndo[iw]->v3List_WLC.push_back(bldg_ptr->zone[iz]->surf[is]->wndo[iw]->vPt3VerticesWCS_OCCW[2]);
                bldg_ptr->zone[iz]->surf[is]->wndo[iw]->v3List_WLC.push_back(bldg_ptr->zone[iz]->surf[is]->wndo[iw]->vPt3VerticesWCS_OCCW[1]);
                bldg_ptr->zone[iz]->surf[is]->wndo[iw]->v3List_WLC.push_back(bldg_ptr->zone[iz]->surf[is]->wndo[iw]->vPt3VerticesWCS_OCCW[0]);
                for (ivert=(iVertices-1); ivert>2; ivert--) {
                    bldg_ptr->zone[iz]->surf[is]->wndo[iw]->v3List_WLC.push_back(bldg_ptr->zone[iz]->surf[is]->wndo[iw]->vPt3VerticesWCS_OCCW[ivert]);
                }

                // Perform WLCWNDOInit, which meshes the window among other initialization tasks.
                // Note that MaxGridNodeArea might get increased if number of nodes exceeds array limits.
                bldg_ptr->zone[iz]->surf[is]->wndo[iw]->WLCWNDOInit(bldg_ptr->zone[iz]->max_grid_node_area);
			}

			/* Read and discard SURFACE CFS headings lines */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);

			/* Read CFS data */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->surf[is]->ncfs);
			if (bldg_ptr->zone[iz]->surf[is]->ncfs > MAX_SURF_CFS) {
				*pofdmpfile << "ERROR: DElight exceeded maximum SURFACE CFS limit of " << MAX_SURF_CFS << "\n";
				return(-1);
			}
			for (int icfs=0; icfs<bldg_ptr->zone[iz]->surf[is]->ncfs; icfs++) {

				/* Read and discard CFS headings lines */
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				fgets(cInputLine, MAX_CHAR_LINE, infile);

				/* Read and store CFS name line */
				char charCFSname[MAX_CHAR_UNAME+1];
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				sscanf_s(cInputLine,"%*s %s\n",charCFSname,_countof(charCFSname));

				// CFS System Parameters
				string strCFSParameters;
				char charCFSParameters[MAX_CHAR_UNAME+1];
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				sscanf_s(cInputLine,"%*s %s\n",charCFSParameters,_countof(charCFSParameters));
				strCFSParameters = charCFSParameters;

				// CFS Rotation
				double dCFSrotation;
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				sscanf_s(cInputLine,"%*s %lf\n",&dCFSrotation);
				
				/* CFS Vertices in World Coordinate System coordinates */
				// Read number of vertices
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				sscanf_s(cInputLine,"%*s %d\n",&iVertices);
	            vector<BGL::point3> vPt3;
				for (ivert=0; ivert<iVertices; ivert++) {
					// Read the coords of each vertex
					fgets(cInputLine, MAX_CHAR_LINE, infile);
					/* tokenize the line label */
					token = strtok_s(cInputLine," ",&next_token);
					for (icoord=0; icoord<NCOORDS; icoord++) {
						token = strtok_s(NULL," ",&next_token);
						dTmpVertex[icoord] = atof(token);
					}
				    // Store the vertex coords as a BGL point3
					p3TmpPt = BGL::point3(dTmpVertex[0], dTmpVertex[1], dTmpVertex[2]);
                    // Add the vertex to the temp vector
                    vPt3.push_back(p3TmpPt);
				}
				// Reorder the vector of vertex coords for the WLC vertex vector order
                // WLC Vertex order begins at inside "lower-left" and is in INSIDE-CounterClockWise order
	            vector<BGL::point3> vPt3WLCOrder;
                vPt3WLCOrder.push_back(vPt3[2]);
                vPt3WLCOrder.push_back(vPt3[1]);
                vPt3WLCOrder.push_back(vPt3[0]);
                for (ivert=(vPt3.size()-1); ivert>2; ivert--) {
                    vPt3WLCOrder.push_back(vPt3[ivert]);
                }

                // Check the WLC vector
                int iWLClistn = vPt3WLCOrder.size();
                for (ivert=0; ivert<iWLClistn; ivert++) {
                    p3TmpPt = vPt3WLCOrder[ivert];
                }

                // Construct CFSSystem and CFSSurface instances from input data

                // First the CFSSystem
                CFSSystem* pNewCFSSystem = NULL;

                // Check to see if this DNA string has been previously used to construct a CFSSystem for this parent SURF
                for (int iCFSSysItem=0; iCFSSysItem < (int)bldg_ptr->zone[iz]->surf[is]->vpCFSSystem.size(); iCFSSysItem++) {
                    if (bldg_ptr->zone[iz]->surf[is]->vpCFSSystem[iCFSSysItem]->TypeName() == strCFSParameters) {
                        pNewCFSSystem = bldg_ptr->zone[iz]->surf[is]->vpCFSSystem[iCFSSysItem];
                        break;
                    }
                }

                // If this DNA string is a newly encountered one, then construct a new CFSSystem
                if (!pNewCFSSystem) {

                    // Fill the LBOS from the input DNA string using the Secret Decoder Ring
	                LumParam lpCFSSystem;
	                if (!SecretDecoderRing(lpCFSSystem,strCFSParameters)) {
				        *pofdmpfile << "ERROR: DElight Invalid CFS Parameter - " << lpCFSSystem.BadName << "\n";
				        return(-1);
	                }

                    // Now construct a CFSSystem based on the LBOS input DNA string
					// Set the hemisphiral resolutions based on previous DElight version of CFSSystem.cpp
					lpCFSSystem.btdfHSResIn = 300;
					lpCFSSystem.btdfHSResOut = 2500;
	                CFSSystem* pNewCFSSystem = new CFSSystem(strCFSParameters, lpCFSSystem);

                    //	Add the CFSSystem pointer to its parent SURF
                    bldg_ptr->zone[iz]->surf[is]->vpCFSSystem.push_back(pNewCFSSystem);
                }

                // Now construct the CFSSurface associated with this CFSSystem
                // Pass in an empty Luminance Map (HemiSphiral()) at this point for efficiency
	            CFSSurface* pNewCFSSurface = new CFSSurface(bldg_ptr->zone[iz]->surf[is], strCFSParameters, HemiSphiral(), dCFSrotation, vPt3WLCOrder, bldg_ptr->zone[iz]->max_grid_node_area);
                // RJH 3-24-04 Hardwire grid area for CFS to 1 ft2 for now
//	            CFSSurface* pNewCFSSurface = new CFSSurface(bldg_ptr->zone[iz]->surf[is], strCFSParameters, HemiSphiral(), dCFSrotation, vPt3WLCOrder, 0.25);

                //	Add the CFSSurface pointer to its parent SURF
                bldg_ptr->zone[iz]->surf[is]->cfs[icfs] = pNewCFSSurface;
			}

            // Perform WLCSURFInit, which meshes the surface excluding window and CFS cutouts, among other initialization tasks.
            // Note that MaxGridNodeArea might get increased if number of nodes exceeds array limits.
            bldg_ptr->zone[iz]->surf[is]->WLCSURFInit(bldg_ptr->zone[iz]->surf[is]->name, bldg_ptr->zone[iz]->max_grid_node_area);
		}

		/* Read and discard REFERENCE POINT headings lines */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		fgets(cInputLine, MAX_CHAR_LINE, infile);

		/* Read reference point data */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->nrefpts);
		if (bldg_ptr->zone[iz]->nrefpts > MAX_REF_PTS) {
			*pofdmpfile << "ERROR: DElight exceeded maximum ZONE REFERENCE POINTS limit of " << MAX_REF_PTS << "\n";
			return(-1);
		}
		for (irp=0; irp<bldg_ptr->zone[iz]->nrefpts; irp++) {
			bldg_ptr->zone[iz]->ref_pt[irp] = new REFPT;
			if (bldg_ptr->zone[iz]->ref_pt[irp] == NULL) {
				*pofdmpfile << "ERROR: DElight Insufficient memory for REFERENCE POINT allocation\n";
				return(-1);
			}
			struct_init("REFPT",(char *)bldg_ptr->zone[iz]->ref_pt[irp]);

			/* Read and discard REFERENCE POINT DATA headings lines */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);

			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %s\n",bldg_ptr->zone[iz]->ref_pt[irp]->name,_countof(bldg_ptr->zone[iz]->ref_pt[irp]->name));
			/* reference point in world system coordinates */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			/* tokenize the line label */
			token = strtok_s(cInputLine," ",&next_token);
			for (icoord=0; icoord<NCOORDS; icoord++) {
				token = strtok_s(NULL," ",&next_token);
				bldg_ptr->zone[iz]->ref_pt[irp]->zs[icoord] = atof(token);
                // All coordinates from EPlus are in WCS so bldg sys = zone sys coords
				bldg_ptr->zone[iz]->ref_pt[irp]->bs[icoord] = bldg_ptr->zone[iz]->ref_pt[irp]->zs[icoord];
			}
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->ref_pt[irp]->zone_frac);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->ref_pt[irp]->lt_set_pt);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->ref_pt[irp]->lt_ctrl_type);
			/* Allocate memory for window luminance factors for each ref_pt<->wndo combination */
			for (is=0; is<bldg_ptr->zone[iz]->nsurfs; is++) {
				for (iw=0; iw<bldg_ptr->zone[iz]->surf[is]->nwndos; iw++) {
					bldg_ptr->zone[iz]->ref_pt[irp]->wlum[is][iw] = new WLUM;
					if (bldg_ptr->zone[iz]->ref_pt[irp]->wlum[is][iw] == NULL) {
						*pofdmpfile << "ERROR: DElight Insufficient memory for WINDOW LUMINANCE allocation\n";
						return(-1);
					}
					struct_init("WLUM",(char *)bldg_ptr->zone[iz]->ref_pt[irp]->wlum[is][iw]);
				}
			}
		}
	}

	/* Read and discard BUILDING SHADES headings lines */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	fgets(cInputLine, MAX_CHAR_LINE, infile);

	/* Read building shade data */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->nbshades);
	if (bldg_ptr->nbshades > MAX_BLDG_SHADES) {
		*pofdmpfile << "ERROR: DElight exceeded maximum BUILDING SHADES limit of " << MAX_BLDG_SHADES << "\n";
		return(-1);
	}
	for (ish=0; ish<bldg_ptr->nbshades; ish++) {
		bldg_ptr->bshade[ish] = new BSHADE;
		if (bldg_ptr->bshade[ish] == NULL) {
			*pofdmpfile << "ERROR: DElight Insufficient memory for BUILDING SHADE allocation\n";
			return(-1);
		}
		struct_init("BSHADE",(char *)bldg_ptr->bshade[ish]);

		/* Read and discard BUILDING SHADE headings lines */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		fgets(cInputLine, MAX_CHAR_LINE, infile);

		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %s\n",bldg_ptr->bshade[ish]->name,_countof(bldg_ptr->bshade[ish]->name));
		/* bldg shade origin in bldg system coordinates */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		/* tokenize the line label */
		token = strtok_s(cInputLine," ",&next_token);
		for (icoord=0; icoord<NCOORDS; icoord++) {
			token = strtok_s(NULL," ",&next_token);
			bldg_ptr->bshade[ish]->origin[icoord] = atof(token);
		}
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->bshade[ish]->height);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->bshade[ish]->width);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->bshade[ish]->azm);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->bshade[ish]->tilt);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->bshade[ish]->vis_refl);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->bshade[ish]->gnd_refl);
	}

	return(0);
}

/****************************** subroutine LoadLibDataFromEPlus *****************************/
/* Loads DElight Library data from a disk file generated from EnergyPlus data. */
/****************************** subroutine LoadLibDataFromEPlus *****************************/
int LoadLibDataFromEPlus(
	LIB *lib_ptr,		/* library structure pointer */
	FILE *infile,		/* pointer to library data file */
	ofstream* pofdmpfile)	/* ptr to dump file */
{
	char cInputLine[MAX_CHAR_LINE+1];	/* Input line */
	int ig;					/* indexes */

	/* Read and discard heading line */
	fgets(cInputLine, MAX_CHAR_LINE, infile);

	/* Read and discard GLASS TYPES headings lines */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	fgets(cInputLine, MAX_CHAR_LINE, infile);

	/* Read glass type data */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %d\n",&lib_ptr->nglass);
	if (lib_ptr->nglass > MAX_LIB_COMPS) {
		*pofdmpfile << "ERROR: DElight exceeded maximum GLASS TYPES limit of " << MAX_LIB_COMPS << "\n";
		return(-1);
	}
	for (ig=0; ig<lib_ptr->nglass; ig++) {
		lib_ptr->glass[ig] = new GLASS;
		if (lib_ptr->glass[ig] == NULL) {
			*pofdmpfile << "ERROR: DElight Insufficient memory for GLASS allocation\n";
			return(-1);
		}
		struct_init("GLASS",(char *)lib_ptr->glass[ig]);

		/* Read and discard GLASS TYPE DATA headings lines */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		fgets(cInputLine, MAX_CHAR_LINE, infile);

		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %s\n",lib_ptr->glass[ig]->name,_countof(lib_ptr->glass[ig]->name));
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&lib_ptr->glass[ig]->EPlusDiffuse_Trans);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&lib_ptr->glass[ig]->inside_refl);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&lib_ptr->glass[ig]->EPlusCoef[0]);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&lib_ptr->glass[ig]->EPlusCoef[1]);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&lib_ptr->glass[ig]->EPlusCoef[2]);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&lib_ptr->glass[ig]->EPlusCoef[3]);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&lib_ptr->glass[ig]->EPlusCoef[4]);
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %lf\n",&lib_ptr->glass[ig]->EPlusCoef[5]);
	}

	return(0);
}

/****************************** subroutine LoadDFs *****************************/
// NOT CURRENTLY USED
/* Loads DElight calculated Daylight Factor data from a disk file */
// into an existing BLDG data structure
/****************************** subroutine LoadDFs *****************************/
int LoadDFs(
	BLDG *bldg_ptr,		/* building structure pointer */
	FILE *infile,		/* pointer to building data file */
	ofstream* pofdmpfile)	/* ptr to dump file */
{
	char cInputLine[MAX_CHAR_LINE+1];	/* Input line */
	char *token;						/* Input token pointer */
	char *next_token;					/* Next token pointer for strtok_s call */

	/* Read and discard the first two lines */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	fgets(cInputLine, MAX_CHAR_LINE, infile);

	/* Read and discard ZONES headings lines */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	fgets(cInputLine, MAX_CHAR_LINE, infile);

	/* Read zone data */
	fgets(cInputLine, MAX_CHAR_LINE, infile);
	sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->nzones);
	for (int iz=0; iz<bldg_ptr->nzones; iz++) {

		/* Read and discard ZONE headings lines */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		fgets(cInputLine, MAX_CHAR_LINE, infile);

		// Read the Zone Name line
		fgets(cInputLine, MAX_CHAR_LINE, infile);

		/* Read and discard REFERENCE POINT headings lines */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		fgets(cInputLine, MAX_CHAR_LINE, infile);

		/* Read reference point data */
		fgets(cInputLine, MAX_CHAR_LINE, infile);
		sscanf_s(cInputLine,"%*s %d\n",&bldg_ptr->zone[iz]->nrefpts);
		for (int irp=0; irp<bldg_ptr->zone[iz]->nrefpts; irp++) {

			/* Read and discard REFERENCE POINT DATA headings lines */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);

			// Read the Reference Point Name line
			fgets(cInputLine, MAX_CHAR_LINE, infile);

			/* Read Reference_Point_Daylight_Factor_for_Overcast_Sky lines */
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			sscanf_s(cInputLine,"%*s %lf\n",&bldg_ptr->zone[iz]->ref_pt[irp]->dfskyo);

			// Read the Reference Point Daylight Factors for Clear Sky heading lines
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);

			/* Read the data for Reference Point Daylight Factors for Clear Sky */
			int isunalt;
			for (isunalt=0; isunalt<NPHS; isunalt++) {
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				/* tokenize the line label */
				token = strtok_s(cInputLine," ",&next_token);
				// Now tokenize the clear sky DFs
				for (int isunazm=0; isunazm<NTHS; isunazm++) {
					token = strtok_s(NULL," ",&next_token);
					bldg_ptr->zone[iz]->ref_pt[irp]->dfsky[isunalt][isunazm] = atof(token);
				}
			}

			// Read the Reference Point Daylight Factors for Clear Sun heading lines
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);
			fgets(cInputLine, MAX_CHAR_LINE, infile);

			/* Read the data for Reference Point Daylight Factors for Clear Sun */
			for (isunalt=0; isunalt<NPHS; isunalt++) {
				fgets(cInputLine, MAX_CHAR_LINE, infile);
				/* tokenize the line label */
				token = strtok_s(cInputLine," ",&next_token);
				// Now tokenize the clear sun DFs
				for (int isunazm=0; isunazm<NTHS; isunazm++) {
					token = strtok_s(NULL," ",&next_token);
					bldg_ptr->zone[iz]->ref_pt[irp]->dfsun[isunalt][isunazm] = atof(token);
				}
			}
		}
	}

	return(0);
}