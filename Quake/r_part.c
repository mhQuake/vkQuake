/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2016 Axel Gneiting

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "quakedef.h"

typedef struct ptypedef_s {
	vec3_t dvel;
	vec3_t grav;
	int *ramp;
	float ramptime;
	vec3_t accel;
} ptypedef_t;


typedef enum {
	pt_static, pt_grav, pt_slowgrav, pt_fire, pt_explode, pt_explode2, pt_blob, pt_blob2, pt_entparticles, MAX_PARTICLE_TYPES
} ptype_t;


// ramps are made larger to allow for overshoot, using full alpha as the dead particle; anything exceeding ramp[8] is automatically dead
int ramp1[] = {0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
int ramp2[] = {0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
int ramp3[] = {0x6d, 0x6b, 0x06, 0x05, 0x04, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


ptypedef_t p_typedefs[MAX_PARTICLE_TYPES] = {
	{{0, 0, 0}, {0, 0, 0}, NULL, 0},		// pt_static
	{{0, 0, 0}, {0, 0, -1}, NULL, 0},		// pt_grav
	{{0, 0, 0}, {0, 0, -0.5}, NULL, 0},		// pt_slowgrav
	{{0, 0, 0}, {0, 0, 1}, ramp3, 5},		// pt_fire
	{{4, 4, 4}, {0, 0, -1}, ramp1, 10},		// pt_explode
	{{-1, -1, -1}, {0, 0, -1}, ramp2, 15},		// pt_explode2
	{{4, 4, 4}, {0, 0, -1}, NULL, 0},		// pt_blob
	{{-4, -4, 0}, {0, 0, -1}, NULL, 0},		// pt_blob2
	{{4, 4, 4}, {0, 0, -1}, NULL, 0},		// pt_entparticles replicates pt_explode but with no ramp
};


// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct particle_s
{
	// driver-usable fields
	vec3_t org;
	int color;

	// drivers never touch the following fields
	vec3_t vel;
	float ramp;
	float time;
	float die;
	ptype_t type;

	int flags;

	struct particle_s *next;
} particle_t;


typedef struct particlevertex_s
{
	float position[3];
	unsigned color;
} particlevertex_t;


#define MAX_PARTICLES			2048	// default max # of particles at one time

#define PF_ONEFRAME		(1 << 0)		// particle is removed after one frame irrespective of die times
#define PF_ETERNAL		(1 << 1)		// particle is never removed irrespective of die times



particle_t *active_particles;
particle_t *free_particles;


particle_t *R_AllocParticle (void)
{
	if (!free_particles)
	{
		int i;

		free_particles = (particle_t *) Hunk_Alloc (MAX_PARTICLES * sizeof (particle_t));

		for (i = 1; i < MAX_PARTICLES; i++)
			free_particles[i - 1].next = &free_particles[i];

		free_particles[MAX_PARTICLES - 1].next = NULL;

		// call recursively to use the new free particles
		return R_AllocParticle ();
	}
	else
	{
		// take the first free particle
		particle_t *p = free_particles;
		free_particles = p->next;

		// and move it to the active list
		p->next = active_particles;
		active_particles = p;

		// save off time the particle was spawned at for time-based effects
		p->time = cl.time;

		// initially no flags
		p->flags = 0;

		// and return what we got
		return p;
	}
}


cvar_t	r_particles = {"r_particles","1", CVAR_ARCHIVE}; //johnfitz

extern cvar_t r_showtris;


/*
===============
R_InitParticles
===============
*/
void R_InitParticles (void)
{
	Cvar_RegisterVariable (&r_particles); //johnfitz
}


/*
===============
R_ClearParticles
===============
*/
void R_ClearParticles (void)
{
	free_particles = NULL;
	active_particles = NULL;
}


/*
===============
R_EntityParticles
===============
*/
#define NUMVERTEXNORMALS	162
extern	float	r_avertexnormals[NUMVERTEXNORMALS][3];
vec3_t	avelocities[NUMVERTEXNORMALS];
float	beamlength = 16;
vec3_t	avelocity = {23, 7, 3};
float	partstep = 0.01;
float	timescale = 0.01;

void R_EntityParticles (entity_t *ent)
{
	int		i;
	particle_t	*p;
	float		angle;
	float		sp, sy, cp, cy;
//	float		sr, cr;
//	int		count;
	vec3_t		forward;
	float		dist;

	dist = 64;
//	count = 50;

	if (!avelocities[0][0])
	{
		for (i = 0; i < NUMVERTEXNORMALS; i++)
		{
			avelocities[i][0] = (rand() & 255) * 0.01;
			avelocities[i][1] = (rand() & 255) * 0.01;
			avelocities[i][2] = (rand() & 255) * 0.01;
		}
	}

	for (i = 0; i < NUMVERTEXNORMALS; i++)
	{
		angle = cl.time * avelocities[i][0];
		sy = sin(angle);
		cy = cos(angle);
		angle = cl.time * avelocities[i][1];
		sp = sin(angle);
		cp = cos(angle);
		angle = cl.time * avelocities[i][2];
	//	sr = sin(angle);
	//	cr = cos(angle);

		forward[0] = cp*cy;
		forward[1] = cp*sy;
		forward[2] = -sp;

		if ((p = R_AllocParticle ()) == NULL)
			return;

		p->die = cl.time + 0.01;
		p->color = 0x6f;
		p->type = pt_entparticles;

		p->org[0] = ent->origin[0] + r_avertexnormals[i][0]*dist + forward[0]*beamlength;
		p->org[1] = ent->origin[1] + r_avertexnormals[i][1]*dist + forward[1]*beamlength;
		p->org[2] = ent->origin[2] + r_avertexnormals[i][2]*dist + forward[2]*beamlength;

		p->flags = PF_ONEFRAME;
	}
}


/*
===============
R_ReadPointFile_f
===============
*/
void R_ReadPointFile_f (void)
{
	FILE	*f;
	vec3_t	org;
	int		r;
	int		c;
	particle_t	*p;
	char	name[MAX_QPATH];

	if (cls.state != ca_connected)
		return;			// need an active map.

	q_snprintf (name, sizeof(name), "maps/%s.pts", cl.mapname);

	COM_FOpenFile (name, &f, NULL);

	if (!f)
	{
		Con_Printf ("couldn't open %s\n", name);
		return;
	}

	Con_Printf ("Reading %s...\n", name);

	c = 0;
	org[0] = org[1] = org[2] = 0; // silence pesky compiler warnings

	for ( ;; )
	{
		r = fscanf (f,"%f %f %f\n", &org[0], &org[1], &org[2]);
		if (r != 3)
			break;
		c++;

		if ((p = R_AllocParticle ()) == NULL)
		{
			Con_Printf ("Not enough free particles\n");
			break;
		}

		p->die = 99999;
		p->color = (-c)&15;
		p->type = pt_static;

		VectorCopy (vec3_origin, p->vel);
		VectorCopy (org, p->org);

		p->flags = PF_ETERNAL;
	}

	fclose (f);
	Con_Printf ("%i points read\n", c);
}

/*
===============
R_ParseParticleEffect

Parse an effect out of the server message
===============
*/
void R_ParseParticleEffect (void)
{
	vec3_t		org, dir;
	int			i, count, color;

	for (i=0 ; i<3 ; i++)
		org[i] = MSG_ReadCoord (cl.protocolflags);
	for (i=0 ; i<3 ; i++)
		dir[i] = MSG_ReadChar () * (1.0/16);
	count = MSG_ReadByte ();
	color = MSG_ReadByte ();

	if (count == 255)
		R_ParticleExplosion (org);
	else R_RunParticleEffect (org, dir, color, count);
}

/*
===============
R_ParticleExplosion
===============
*/
void R_ParticleExplosion (vec3_t org)
{
	int			i, j;
	particle_t	*p;

	for (i=0 ; i<1024 ; i++)
	{
		if ((p = R_AllocParticle ()) == NULL)
			return;

		p->die = cl.time + 5;
		p->color = ramp1[0];
		p->ramp = rand()&3;
		if (i & 1)
		{
			p->type = pt_explode;
			for (j=0 ; j<3 ; j++)
			{
				p->org[j] = org[j] + ((rand()%32)-16);
				p->vel[j] = (rand()%512)-256;
			}
		}
		else
		{
			p->type = pt_explode2;
			for (j=0 ; j<3 ; j++)
			{
				p->org[j] = org[j] + ((rand()%32)-16);
				p->vel[j] = (rand()%512)-256;
			}
		}
	}
}

/*
===============
R_ParticleExplosion2
===============
*/
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength)
{
	int			i, j;
	particle_t	*p;
	int			colorMod = 0;

	for (i=0; i<512; i++)
	{
		if ((p = R_AllocParticle ()) == NULL)
			return;

		p->die = cl.time + 0.3;
		p->color = colorStart + (colorMod % colorLength);
		colorMod++;

		p->type = pt_blob;
		for (j=0 ; j<3 ; j++)
		{
			p->org[j] = org[j] + ((rand()%32)-16);
			p->vel[j] = (rand()%512)-256;
		}
	}
}

/*
===============
R_BlobExplosion
===============
*/
void R_BlobExplosion (vec3_t org)
{
	int			i, j;
	particle_t	*p;

	for (i=0 ; i<1024 ; i++)
	{
		if ((p = R_AllocParticle ()) == NULL)
			return;

		p->die = cl.time + 1 + (rand()&8)*0.05;

		if (i & 1)
		{
			p->type = pt_blob;
			p->color = 66 + rand()%6;
			for (j=0 ; j<3 ; j++)
			{
				p->org[j] = org[j] + ((rand()%32)-16);
				p->vel[j] = (rand()%512)-256;
			}
		}
		else
		{
			p->type = pt_blob2;
			p->color = 150 + rand()%6;
			for (j=0 ; j<3 ; j++)
			{
				p->org[j] = org[j] + ((rand()%32)-16);
				p->vel[j] = (rand()%512)-256;
			}
		}
	}
}

/*
===============
R_RunParticleEffect
===============
*/
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count)
{
	int			i, j;
	particle_t	*p;

	if (count == 1024)
	{
		R_ParticleExplosion (org);
		return;
	}

	for (i=0 ; i<count ; i++)
	{
		if ((p = R_AllocParticle ()) == NULL)
			return;

		p->die = cl.time + 0.1*(rand()%5);
		p->color = (color&~7) + (rand()&7);
		p->type = pt_slowgrav;

		for (j=0 ; j<3 ; j++)
		{
			p->org[j] = org[j] + ((rand()&15)-8);
			p->vel[j] = dir[j]*15;// + (rand()%300)-150;
		}
	}
}


/*
===============
R_LavaSplash
===============
*/
void R_LavaSplash (vec3_t org)
{
	int			i, j, k;
	particle_t	*p;
	float		vel;
	vec3_t		dir;

	for (i=-16 ; i<16 ; i++)
		for (j=-16 ; j<16 ; j++)
			for (k=0 ; k<1 ; k++)
			{
				if ((p = R_AllocParticle ()) == NULL)
					return;

				p->die = cl.time + 2 + (rand()&31) * 0.02;
				p->color = 224 + (rand()&7);
				p->type = pt_slowgrav;

				dir[0] = j*8 + (rand()&7);
				dir[1] = i*8 + (rand()&7);
				dir[2] = 256;

				p->org[0] = org[0] + dir[0];
				p->org[1] = org[1] + dir[1];
				p->org[2] = org[2] + (rand()&63);

				VectorNormalize (dir);
				vel = 50 + (rand()&63);
				VectorScale (dir, vel, p->vel);
			}
}

/*
===============
R_TeleportSplash
===============
*/
void R_TeleportSplash (vec3_t org)
{
	int			i, j, k;
	particle_t	*p;
	float		vel;
	vec3_t		dir;

	for (i=-16 ; i<16 ; i+=4)
		for (j=-16 ; j<16 ; j+=4)
			for (k=-24 ; k<32 ; k+=4)
			{
				if ((p = R_AllocParticle ()) == NULL)
					return;

				p->die = cl.time + 0.2 + (rand()&7) * 0.02;
				p->color = 7 + (rand()&7);
				p->type = pt_slowgrav;

				dir[0] = j*8;
				dir[1] = i*8;
				dir[2] = k*8;

				p->org[0] = org[0] + i + (rand()&3);
				p->org[1] = org[1] + j + (rand()&3);
				p->org[2] = org[2] + k + (rand()&3);

				VectorNormalize (dir);
				vel = 50 + (rand()&63);
				VectorScale (dir, vel, p->vel);
			}
}

/*
===============
R_RocketTrail

FIXME -- rename function and use #defined types instead of numbers
===============
*/
void R_RocketTrail (vec3_t start, vec3_t end, int type)
{
	vec3_t		vec;
	float		len;
	int			j;
	particle_t	*p;
	int			dec;
	static int	tracercount;

	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);
	if (type < 128)
		dec = 3;
	else
	{
		dec = 1;
		type -= 128;
	}

	while (len > 0)
	{
		len -= dec;

		if ((p = R_AllocParticle ()) == NULL)
			return;

		VectorCopy (vec3_origin, p->vel);
		p->die = cl.time + 2;

		switch (type)
		{
			case 0:	// rocket trail
				p->ramp = (rand()&3);
				p->color = ramp3[(int)p->ramp];
				p->type = pt_fire;
				for (j=0 ; j<3 ; j++)
					p->org[j] = start[j] + ((rand()%6)-3);
				break;

			case 1:	// smoke smoke
				p->ramp = (rand()&3) + 2;
				p->color = ramp3[(int)p->ramp];
				p->type = pt_fire;
				for (j=0 ; j<3 ; j++)
					p->org[j] = start[j] + ((rand()%6)-3);
				break;

			case 2:	// blood
				p->type = pt_grav;
				p->color = 67 + (rand()&3);
				for (j=0 ; j<3 ; j++)
					p->org[j] = start[j] + ((rand()%6)-3);
				break;

			case 3:
			case 5:	// tracer
				p->die = cl.time + 0.5;
				p->type = pt_static;
				if (type == 3)
					p->color = 52 + ((tracercount&4)<<1);
				else
					p->color = 230 + ((tracercount&4)<<1);

				tracercount++;

				VectorCopy (start, p->org);
				if (tracercount & 1)
				{
					p->vel[0] = 30*vec[1];
					p->vel[1] = 30*-vec[0];
				}
				else
				{
					p->vel[0] = 30*-vec[1];
					p->vel[1] = 30*vec[0];
				}
				break;

			case 4:	// slight blood
				p->type = pt_grav;
				p->color = 67 + (rand()&3);
				for (j=0 ; j<3 ; j++)
					p->org[j] = start[j] + ((rand()%6)-3);
				len -= 3;
				break;

			case 6:	// voor trail
				p->color = 9*16 + 8 + (rand()&3);
				p->type = pt_static;
				p->die = cl.time + 0.3;
				for (j=0 ; j<3 ; j++)
					p->org[j] = start[j] + ((rand()&15)-8);
				break;
		}

		VectorAdd (start, vec, start);
	}
}

/*
===============
CL_RunParticles -- johnfitz -- all the particle behavior, separated from R_DrawParticles
===============
*/
void CL_RunParticles (void)
{
	for (;;)
	{
		particle_t *kill = active_particles;

		if (kill && kill->die < cl.time)
		{
			active_particles = kill->next;
			kill->next = free_particles;
			free_particles = kill;
			continue;
		}

		break;
	}

	for (particle_t *p = active_particles; p; p = p->next)
	{
		for (;;)
		{
			particle_t *kill = p->next;

			if (kill && kill->die < cl.time)
			{
				p->next = kill->next;
				kill->next = free_particles;
				free_particles = kill;
				continue;
			}

			break;
		}

		// particle movement is now done in-line with the render
	}
}


/*
===============
R_FlushParticles
===============
*/
void R_FlushParticles (int num_particles, particlevertex_t *vertices)
{
	VkBuffer vertex_buffer = 0;
	VkDeviceSize vertex_buffer_offset = 0;

	// allocate buffer space and copy it over
	particlevertex_t *dst = (particlevertex_t *) R_VertexAllocate (num_particles * sizeof (particlevertex_t), &vertex_buffer, &vertex_buffer_offset);
	memcpy (dst, vertices, num_particles * sizeof (particlevertex_t));

	// draw
	vulkan_globals.vk_cmd_bind_vertex_buffers (vulkan_globals.command_buffer, 0, 1, &vertex_buffer, &vertex_buffer_offset);
	vulkan_globals.vk_cmd_draw (vulkan_globals.command_buffer, 4, num_particles, 0, 0);
}


/*
===============
R_DrawParticlesFaces
===============
*/
static void R_DrawParticlesFaces(void)
{
	if (!r_particles.value)
		return;

	if (!active_particles)
		return;

	// update particle accelerations
	for (int i = 0; i < MAX_PARTICLE_TYPES; i++)
	{
		extern cvar_t sv_gravity;
		ptypedef_t *pt = &p_typedefs[i];
		float grav = sv_gravity.value * 0.05;

		// in theory this could be calced once and never again, but in practice mods may change sv_gravity from frame-to-frame
		// so we need to recalc it each frame too.... (correcting the acceleration formula here too)
		pt->accel[0] = (pt->dvel[0] + (pt->grav[0] * grav)) * 0.5f;
		pt->accel[1] = (pt->dvel[1] + (pt->grav[1] * grav)) * 0.5f;
		pt->accel[2] = (pt->dvel[2] + (pt->grav[2] * grav)) * 0.5f;
	}

	static particlevertex_t vertices[MAX_PARTICLES];
	int num_particles = 0;

	// push-constants must be 4-float aligned; we pack them tighter and unpack in the GLSL
	float pcdata[12];

	// these copy over directly
	VectorCopy (vpn, &pcdata[0]);
	VectorCopy (vright, &pcdata[4]);
	VectorCopy (vup, &pcdata[8]);

	// break up the origin so that we can confirm with alignment requirements
	pcdata[3] = r_origin[0];
	pcdata[7] = r_origin[1];
	pcdata[11] = r_origin[2];

	// and set the constants
	R_PushConstants (VK_SHADER_STAGE_ALL_GRAPHICS, 20 * sizeof (float), 12 * sizeof (float), pcdata);

	for (particle_t *p = active_particles; p; p = p->next)
	{
		// get the emitter properties for this particle
		ptypedef_t *pt = &p_typedefs[p->type];
		float etime = cl.time - p->time;

		// update colour ramps
		if (pt->ramp)
		{
			int ramp = (int) (p->ramp + (etime * pt->ramptime));

			// set dead particles to full-alpha and the system will remove them on the next frame
			if (ramp > 8)
			{
				p->color = 0xff;
				p->die = -1;
			}
			else if ((p->color = pt->ramp[ramp]) == 0xff)
				p->die = -1;
		}

		// check for overflow and flush if required
		if (num_particles + 1 >= MAX_PARTICLES)
		{
			R_FlushParticles (num_particles, vertices);
			num_particles = 0;
		}

		// move the particle in a framerate-independent manner (this could go to the GPU as a tradeoff vs a larger vertex struct; it's 50/50 for perf)
		vertices[num_particles].position[0] = p->org[0] + (p->vel[0] + (pt->accel[0] * etime)) * etime;
		vertices[num_particles].position[1] = p->org[1] + (p->vel[1] + (pt->accel[1] * etime)) * etime;
		vertices[num_particles].position[2] = p->org[2] + (p->vel[2] + (pt->accel[2] * etime)) * etime;

		// retrieve the colour
		vertices[num_particles].color = d_8to24table[p->color];

		// update counts
		rs_particles++;
		num_particles++;

		// update die flags - these must be done after drawing, not in CL_RunParticles, as otherwise they will be killed before getting to here!!!
		if (p->flags & PF_ETERNAL) p->die = cl.time + 1;
		if (p->flags & PF_ONEFRAME) p->die = -1;
	}

	if (num_particles)
		R_FlushParticles (num_particles, vertices);
}


/*
===============
R_DrawParticles -- johnfitz -- moved all non-drawing code to CL_RunParticles
===============
*/
void R_DrawParticles (void)
{
	R_BeginDebugUtilsLabel ("Particles");
	R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.particle_pipeline);
	R_DrawParticlesFaces();
	R_EndDebugUtilsLabel ();
}

/*
===============
R_DrawParticles_ShowTris -- johnfitz
===============
*/
void R_DrawParticles_ShowTris (void)
{
	if (r_showtris.value == 1)
		R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_pipeline);
	else
		R_BindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, vulkan_globals.showtris_depth_test_pipeline);

	R_DrawParticlesFaces();
}
