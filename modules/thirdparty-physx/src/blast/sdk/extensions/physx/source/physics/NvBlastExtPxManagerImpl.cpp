﻿#include <pch.h>

#include "NvBlastExtPxManagerImpl.h"
#include "NvBlastExtPxAssetImpl.h"
#include "NvBlastExtPxActorImpl.h"
#include "NvBlastExtPxCollisionBuilderImpl.h"
#include "NvBlastExtPxFamilyImpl.h"

#include "NvBlastAssert.h"

#include "NvBlastTkActor.h"
#include "NvBlastTkFamily.h"
#include "NvBlastTkGroup.h"
#include "NvBlastTkJoint.h"

#include "PxPhysics.h"
#include "PxRigidDynamic.h"
#include "extensions/PxJoint.h"


namespace Nv
{
namespace Blast
{


ExtPxManager* ExtPxManager::create(PxPhysics& physics, TkFramework& framework, ExtPxCreateJointFunction createFn, bool useUserData)
{
	return NVBLAST_NEW(ExtPxManagerImpl)(physics, framework, createFn, useUserData);
}

ExtPxCollisionBuilder* ExtPxManager::createCollisionBuilder(PxPhysics& physics)
{
	return NVBLAST_NEW(ExtPxCollisionBuilderImpl);
}

void ExtPxManagerImpl::release()
{
	NVBLAST_DELETE(this, ExtPxManagerImpl);
}

ExtPxFamily* ExtPxManagerImpl::createFamily(const ExtPxFamilyDesc& desc)
{
	NVBLAST_CHECK_ERROR(desc.pxAsset != nullptr, "Family creation: pxAsset is nullptr.", return nullptr);

	// prepare TkActorDesc (take NvBlastActorDesc from ExtPxFamilyDesc if it's not null, otherwise take from PxAsset)
	TkActorDesc tkActorDesc;
	const NvBlastActorDesc& actorDesc = desc.actorDesc ? *desc.actorDesc : desc.pxAsset->getDefaultActorDesc();
	(&tkActorDesc)->NvBlastActorDesc::operator=(actorDesc);
	tkActorDesc.asset = &desc.pxAsset->getTkAsset();

	// create tk actor
	TkActor* actor = m_framework.createActor(tkActorDesc);
	NVBLAST_CHECK_ERROR(actor != nullptr, "Family creation: tk actor creation failed.", return nullptr);

	// create px family
	ExtPxFamilyImpl* family = NVBLAST_NEW(ExtPxFamilyImpl)(*this, actor->getFamily(), *desc.pxAsset);

	if (desc.group)
	{
		desc.group->addActor(*actor);
	}

	return family;
}

bool ExtPxManagerImpl::createJoint(TkJoint& joint)
{
	if (!joint.userData && m_createJointFn)
	{
		const TkJointData data = joint.getData();
		ExtPxActorImpl* pxActor0 = data.actors[0] != nullptr ? reinterpret_cast<ExtPxActorImpl*>(data.actors[0]->userData) : nullptr;
		ExtPxActorImpl* pxActor1 = data.actors[1] != nullptr ? reinterpret_cast<ExtPxActorImpl*>(data.actors[1]->userData) : nullptr;
		if (!pxActor0 && !pxActor1)
		{
			for (int i = 0; i < 2; ++i)
			{
				if (data.actors[i] != nullptr)
				{
					m_incompleteJointMultiMap[data.actors[i]].pushBack(&joint);
				}
			}
			return false;
		}
		PxTransform lf0(PxVec3(data.attachPositions[0].x, data.attachPositions[0].y, data.attachPositions[0].z));
		PxTransform lf1(PxVec3(data.attachPositions[1].x, data.attachPositions[1].y, data.attachPositions[1].z));
		PxJoint* pxJoint = m_createJointFn(pxActor0,  lf0, pxActor1, lf1, m_physics, joint);
		if (pxJoint)
		{
			joint.userData = pxJoint;
			return true;
		}
	}
	return false;
}

void ExtPxManagerImpl::updateJoint(TkJoint& joint)
{
	const TkJointData& data = joint.getData();
	if (joint.userData)
	{
		ExtPxActorImpl* pxActors[2];
		for (int i = 0; i < 2; ++i)
		{
			if (data.actors[i] != nullptr)
			{
				pxActors[i] = reinterpret_cast<ExtPxActorImpl*>(data.actors[i]->userData);
				if (pxActors[i] == nullptr)
				{
					Array<TkJoint*>::type& joints = m_incompleteJointMultiMap[data.actors[i]];
					NVBLAST_ASSERT(joints.find(&joint) == joints.end());
					joints.pushBack(&joint);
					return;	// Wait until the TkActor is received to create this joint
				}
			}
			else
			{
				pxActors[i] = nullptr;
			}
		}
		NVBLAST_ASSERT(pxActors[0] || pxActors[1]);
		PxJoint* pxJoint = reinterpret_cast<PxJoint*>(joint.userData);
		pxJoint->setActors(pxActors[0] ? &pxActors[0]->getPhysXActor() : nullptr, pxActors[1] ? &pxActors[1]->getPhysXActor() : nullptr);
	}
	else
	{
		ExtPxActorImpl* pxActor0 = data.actors[0] != nullptr ? reinterpret_cast<ExtPxActorImpl*>(data.actors[0]->userData) : nullptr;
		ExtPxActorImpl* pxActor1 = data.actors[1] != nullptr ? reinterpret_cast<ExtPxActorImpl*>(data.actors[1]->userData) : nullptr;
		PxTransform lf0(PxVec3(data.attachPositions[0].x, data.attachPositions[0].y, data.attachPositions[0].z));
		PxTransform lf1(PxVec3(data.attachPositions[1].x, data.attachPositions[1].y, data.attachPositions[1].z));
		PxJoint* pxJoint = m_createJointFn(pxActor0, lf0, pxActor1, lf1, m_physics, joint);
		if (pxJoint)
		{
			joint.userData = pxJoint;
		}
	}
}

void ExtPxManagerImpl::destroyJoint(TkJoint& joint)
{
	if (joint.userData)
	{
		PxJoint* pxJoint = reinterpret_cast<PxJoint*>(joint.userData);
		pxJoint->release();
		joint.userData = nullptr;
	}
}



} // namespace Blast
} // namespace Nv
