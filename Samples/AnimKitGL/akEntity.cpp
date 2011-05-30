/*
-------------------------------------------------------------------------------
    This file is part of OgreKit.
    http://gamekit.googlecode.com/

    Copyright (c) 2006-2010 Xavier T.

    Contributor(s): none yet.
-------------------------------------------------------------------------------
  This software is provided 'as-is', without any express or implied
  warranty. In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
-------------------------------------------------------------------------------
*/



#define GL_GLEXT_PROTOTYPES

#ifdef WIN32
//#include <Windows.h>
#include <GL/glut.h>
#elif defined(__APPLE__)
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "GL/glext.h"

#include "akEntity.h"
#include "akMesh.h"
#include "akSkeleton.h"
#include "akSkeletonPose.h"
#include "akDualQuat.h"
#include "akPose.h"

akEntity::akEntity() : m_mesh(0), m_skeleton(0), m_pose(0), m_useDualQuatSkinning(false),
						m_posAnimated(false), m_morphAnimated(false), m_useVbo(false)
{
}

akEntity::~akEntity()
{
	if(m_pose)
		delete m_pose;
	
	m_matrixPalette.clear();
	m_dualquatPalette.clear();
}

bool akEntity::isMeshDeformedByMorphing(void)
{
	return m_mesh->hasMorphTargets();
}

void akEntity::setSkeleton(akSkeleton *skel)
{
	m_skeleton = skel;
}

void akEntity::init(bool useVbo)
{
	m_useVbo = useVbo;
	
	if(m_skeleton)
	{
		m_pose = new akPose(m_skeleton);
		m_matrixPalette.resize(m_skeleton->getNumJoints());
		m_dualquatPalette.resize(m_skeleton->getNumJoints());
	}
	else
		m_pose = new akPose();
	
	if(isMeshDeformed())
		m_mesh->addSecondPositionBuffer();
	
	if(m_useVbo)
	{
		int nsub = m_mesh->getNumSubMeshes();
		
		m_posnoVertexVboIds.resize(nsub);
		m_staticVertexVboIds.resize(nsub);
		m_staticIndexVboIds.resize(nsub);
		
		glGenBuffers(nsub, &m_posnoVertexVboIds[0]);
		glGenBuffers(nsub, &m_staticVertexVboIds[0]);
		glGenBuffers(nsub, &m_staticIndexVboIds[0]);
		
		for(int i=0; i<nsub; i++)
		{
			akSubMesh* sub = m_mesh->getSubMesh(i);
			UTuint32 nv = sub->getVertexCount();
			UTuint32 ni = sub->getIndexCount();
			
			void *posnodata = sub->getPosNoDataPtr();
			void *staticdata = sub->getStaticVertexDataPtr();
			void *idata = sub->getIndexDataPtr();
			UTuint32 posnodatas = sub->getPosNoDataStride();
			UTuint32 staticdatas = sub->getStaticVertexDataStride();
			UTuint32 idatas = sub->getIndexDataStride();

			glBindBuffer(GL_ARRAY_BUFFER_ARB, m_posnoVertexVboIds[i]);
			glBufferData(GL_ARRAY_BUFFER_ARB, nv*posnodatas, posnodata, GL_STATIC_DRAW);
			
			glBindBuffer(GL_ARRAY_BUFFER_ARB, m_staticVertexVboIds[i]);
			glBufferData(GL_ARRAY_BUFFER_ARB, nv*staticdatas, staticdata, GL_STATIC_DRAW);
			
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER_ARB, m_staticIndexVboIds[i]);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER_ARB, ni*idatas, idata, GL_STATIC_DRAW);
		}
	}
	
	// Activate the first action of each object
	akAnimationPlayer* player = m_players.getAnimationPlayer(0);
	if(player)
	{
		player->setEnabled(true);
		player->setWeight(1.0f);
		player->setMode(akAnimationPlayer::AK_ACT_LOOP);
	}
}

void akEntity::updateVBO(void)
{
	if(m_useVbo)
	{
		int nsub = m_mesh->getNumSubMeshes();
		
		for(int i=0; i<nsub; i++)
		{
			akSubMesh* sub = m_mesh->getSubMesh(i);
			UTuint32 nv = sub->getVertexCount();
			void *codata = sub->getSecondPosNoDataPtr();
			UTuint32 datas = sub->getPosNoDataStride();
			
			glBindBuffer(GL_ARRAY_BUFFER_ARB, m_posnoVertexVboIds[i]);
			glBufferData(GL_ARRAY_BUFFER_ARB, nv*datas, NULL, GL_STREAM_DRAW);
			glBufferSubData(GL_ARRAY_BUFFER_ARB, 0, nv*datas, codata);
		}
		glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
	}
}

void akEntity::step(akScalar dt, int dualQuat, int normalsMethod)
{
	m_pose->reset(akSkeletonPose::SP_BINDING_SPACE);
	m_players.stepTime(dt);
	m_players.evaluate(m_pose);
		
	if(isPositionAnimated())
		setTransformState(m_pose->getTransform());
	
	if(isMeshDeformed())
	{
		akSkeletonPose* skelpose = m_pose->getSkeletonPose();
		skelpose->toModelSpace(skelpose);
		
		int dq ;
		switch(dualQuat)
		{
		case 0:
			if(getUseDualQuatSkinning())
				dq = akGeometryDeformer::GD_SO_DUAL_QUAT;
			else
				dq = akGeometryDeformer::GD_SO_MATRIX;
			break;
		case 1:
			dq = akGeometryDeformer::GD_SO_MATRIX;
			break;
		case 2:
			dq = akGeometryDeformer::GD_SO_DUAL_QUAT;
			break;
		case 3:
			dq = akGeometryDeformer::GD_SO_DUAL_QUAT_ANTIPOD;
			break;
		}
		int nm;
		switch(normalsMethod)
		{
		case 0:
			nm = akGeometryDeformer::GD_NO_NONE;
			break;
		case 1:
			nm = akGeometryDeformer::GD_NO_NOSCALE;
			break;
		case 2:
			nm = akGeometryDeformer::GD_NO_UNIFORM_SCALE;
			break;
		case 3:
			nm = akGeometryDeformer::GD_NO_FULL;
			break;
		}

		if( dq != akGeometryDeformer::GD_SO_MATRIX )
			m_pose->fillDualQuatPalette(m_dualquatPalette, m_matrixPalette);
		else
			m_pose->fillMatrixPalette(m_matrixPalette);
			
		m_mesh->deform((akGeometryDeformer::SkinningOption)dq, (akGeometryDeformer::NormalsOption)nm,
					   m_pose, &m_matrixPalette, &m_dualquatPalette);
		
		updateVBO();
	}
}

void akEntity::draw(bool drawNormal, bool drawColor, bool textured, bool useVbo, bool shaded)
{
	glPushMatrix();
	
	akMatrix4 mat = m_transform.toMatrix();
	glMultMatrixf((GLfloat*)&mat);
	
	if(m_mesh)
	{
		for(unsigned int j=0; j< m_mesh->getNumSubMeshes(); j++)
		{
			akSubMesh* sm = m_mesh->getSubMesh(j);
			const akBufferInfo* vbuf = sm->getVertexBuffer();
			const akBufferInfo* ibuf = sm->getIndexBuffer();
			UTuint32 tot = ibuf->getSize();
			
			bool color = drawColor && sm->hasVertexColor();
			bool hasnormals = sm->hasNormals();
			bool texture = textured && sm->getUVLayerCount() >=1;
	
			const akBufferInfo::Element *posbuf, *norbuf, *idxbuf, *uvbuf, *colorbuf;
			
			if(isMeshDeformed())
			{
				posbuf = vbuf->getElement(akBufferInfo::BI_DU_VERTEX, akBufferInfo::VB_DT_3FLOAT32, 2);
				norbuf = vbuf->getElement(akBufferInfo::BI_DU_NORMAL, akBufferInfo::VB_DT_3FLOAT32, 2);
			}
			else
			{
				posbuf = vbuf->getElement(akBufferInfo::BI_DU_VERTEX, akBufferInfo::VB_DT_3FLOAT32, 1);
				norbuf = vbuf->getElement(akBufferInfo::BI_DU_NORMAL, akBufferInfo::VB_DT_3FLOAT32, 1);
			}
			
			uvbuf = vbuf->getElement(akBufferInfo::BI_DU_UV, akBufferInfo::VB_DT_2FLOAT32, 1);
			colorbuf = vbuf->getElement(akBufferInfo::BI_DU_COLOR, akBufferInfo::VB_DT_4UINT8, 1);
			
			idxbuf = ibuf->getElement(akBufferInfo::BI_DU_ELEMENT, akBufferInfo::VB_DT_UINT32, 1);
			
			if(!posbuf ||! idxbuf)
				continue;
			
			if(shaded)
				glEnable(GL_LIGHTING);
			else
				glDisable(GL_LIGHTING);
			
			if(m_useVbo && useVbo)
			{
				glBindBufferARB(GL_ARRAY_BUFFER_ARB, m_posnoVertexVboIds[j]);
				glEnableClientState(GL_VERTEX_ARRAY);
				glVertexPointer(3, GL_FLOAT, posbuf->stride, (GLvoid*)posbuf->getOffset());
				
				if(hasnormals)
				{
					glNormalPointer(GL_FLOAT, norbuf->stride, (GLvoid*)norbuf->getOffset());
					glEnableClientState(GL_NORMAL_ARRAY);
				}
				
				glBindBufferARB(GL_ARRAY_BUFFER_ARB, m_staticVertexVboIds[j]);
				
				if(color)
				{
					glColorPointer(4, GL_UNSIGNED_BYTE, colorbuf->stride, (GLvoid*)colorbuf->getOffset());
					glEnableClientState(GL_COLOR_ARRAY);
				}
				else
				{
					glColor3f(0,0,0);
					glDisableClientState(GL_COLOR_ARRAY);
				}
				
				if(texture)
				{
//					glTexCoordPointer(2, GL_FLOAT, uvstride, );
//					glEnableClientState(GL_TEXTURE_COORD_ARRAY);
				}
				else
				{
					glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				}
				
				glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, m_staticIndexVboIds[j]);
				glDrawElements(GL_TRIANGLES, tot, GL_UNSIGNED_INT, (GLvoid*)idxbuf->getOffset());
				
				glDisableClientState(GL_VERTEX_ARRAY);
				glBindBufferARB(GL_ARRAY_BUFFER_ARB, 0);
				glBindBufferARB(GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
			}
			else
			{
				glBegin(GL_TRIANGLES);
				
				if(!color)
					glColor3f(0,0,0);
					
				int i;
				for(i=0; i<tot; i++)
				{
					UTuint32* id = (UTuint32*)idxbuf->data;
					akVector3* v = (akVector3*)posbuf->data;
					akVector3* n = (akVector3*)norbuf->data;
					UTuint8* c = (UTuint8*)colorbuf->data;
					
					akAdvancePointer(id, i * idxbuf->stride);
					akAdvancePointer(v, *id * posbuf->stride);
					akAdvancePointer(n, *id * norbuf->stride);
					akAdvancePointer(c, *id * colorbuf->stride);
					
					if(color)
						glColor4ub(c[0], c[1], c[2], c[3]);
					
					//if(texture)
						//todo
					
					if(hasnormals)
						glNormal3f(n->getX(), n->getY(), n->getZ());
					
					glVertex3f(v->getX(), v->getY(), v->getZ());
					
				}
				glEnd();
			}
			
			// normals
			if(drawNormal && hasnormals)
			{
				glDisable(GL_LIGHTING);
				glBegin(GL_LINES);
				glColor3f(0, .5, .8);
				
				UTuint32 tot = ibuf->getSize();
				for(unsigned int i=0; i<tot; i++)
				{
					UTuint32* id = (UTuint32*)idxbuf->data;
					akVector3* v = (akVector3*)posbuf->data;
					akVector3* n = (akVector3*)norbuf->data;
					
					akAdvancePointer(id, i * idxbuf->stride);
					akAdvancePointer(v, *id * posbuf->stride);
					akAdvancePointer(n, *id * norbuf->stride);
					
					akVector3 norm = *v + 0.05f * (*n);
					glVertex3f(v->getX(), v->getY(), v->getZ());
					glVertex3f(norm.getX(), norm.getY(), norm.getZ());
				}
				glEnd();
			}
		}
	}
	
	if(m_skeleton)
	{
		glDisable(GL_LIGHTING);
		glDisable(GL_DEPTH_TEST);
		glLineWidth(1.5);
		
		int i, tot;
		tot = m_pose->getSkeletonPose()->getNumJoints();
		for(i=0; i<tot; i++)
		{
			glPushMatrix();
			
			akTransformState* jointpose = m_pose->getSkeletonPose()->getJointPose(i);
			akMatrix4 mat = jointpose->toMatrix();
			glMultMatrixf((GLfloat*)&mat);
	
			glBegin(GL_LINES);
			glColor3f(1,0,0);
			glVertex3f(0.1,0,0);
			glVertex3f(0,0,0);
			
			glColor3f(0,1,0);
			glVertex3f(0,0.1,0);
			glVertex3f(0,0,0);
			
			glColor3f(0,0,1);
			glVertex3f(0,0,0.1);
			glVertex3f(0,0,0);
			glEnd();
			
			glPopMatrix();
		}
		glLineWidth(1);
		glEnable(GL_DEPTH_TEST);
	}
	
	glPopMatrix();
}



