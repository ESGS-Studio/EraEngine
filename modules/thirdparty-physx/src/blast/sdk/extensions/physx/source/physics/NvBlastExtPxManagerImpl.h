#ifndef NVBLASTEXTPXMANAGERIMPL_H
#define NVBLASTEXTPXMANAGERIMPL_H

#include "NvBlastExtPxManager.h"
#include "NvBlastArray.h"
#include "NvBlastHashMap.h"
#include "NvBlastExtPxListener.h"
#include "NvBlastExtPxFamily.h"

#include "PxRigidDynamic.h"


using namespace physx;


namespace Nv
{
namespace Blast
{

// Forward declarations
class TkActor;

class ExtPxManagerImpl final : public ExtPxManager
{
	NV_NOCOPY(ExtPxManagerImpl)

public:
	friend class ExtPxActorImpl;
	friend class ExtPxFamilyImpl;

	ExtPxManagerImpl(PxPhysics& physics, TkFramework&framework, ExtPxCreateJointFunction createFn, bool usePxUserData)
		: m_physics(physics), m_framework(framework), m_createJointFn(createFn), m_usePxUserData(usePxUserData), m_actorCountLimit(0)
	{
	}

	~ExtPxManagerImpl()
	{
	}

	virtual void release() override;


	//////// interface ////////

	virtual ExtPxFamily*	createFamily(const ExtPxFamilyDesc& desc) override;

	virtual bool			createJoint(TkJoint& joint) override;

	virtual void			destroyJoint(TkJoint& joint) override;

	virtual void			setCreateJointFunction(ExtPxCreateJointFunction createFn) override
	{
		m_createJointFn = createFn;
	}

	virtual uint32_t		getFamilyCount() const override
	{
		return m_tkFamiliesMap.size();
	}

	virtual uint32_t		getFamilies(ExtPxFamily** buffer, uint32_t bufferSize) const override
	{
		uint32_t index = 0;
		for (auto it = const_cast<ExtPxManagerImpl*>(this)->m_tkFamiliesMap.getIterator(); !it.done() && index < bufferSize; ++it)
		{
			buffer[index++] = it->second;
		}
		return index;
	}

	virtual ExtPxFamily*	getFamilyFromTkFamily(TkFamily& family) const override
	{
		auto entry = m_tkFamiliesMap.find(&family);
		return entry != nullptr ? entry->second : nullptr;
	}

	virtual ExtPxActor*		getActorFromPhysXActor(const PxRigidDynamic& pxActor) const override
	{
		auto it = m_physXActorsMap.find(&pxActor);
		return it != nullptr ? it->second : nullptr;
	}

	virtual PxPhysics&		getPhysics() const override
	{
		return m_physics;
	}

	virtual TkFramework&	getFramework() const override
	{
		return m_framework;
	}

	virtual bool			isPxUserDataUsed() const override
	{
		return m_usePxUserData;
	}

	virtual void			subscribe(ExtPxListener& listener) override
	{
		m_listeners.pushBack(&listener);
	}

	virtual void			unsubscribe(ExtPxListener& listener) override
	{
		m_listeners.findAndReplaceWithLast(&listener);
	}

	virtual void			setActorCountLimit(uint32_t limit) override
	{
		m_actorCountLimit = limit;
	}

	virtual uint32_t		getActorCountLimit() override
	{
		return m_actorCountLimit;
	}

	virtual uint32_t		getPxActorCount() const override
	{
		return m_physXActorsMap.size();
	}


	//////// internal public methods ////////

	void					registerActor(PxRigidDynamic* pxActor, ExtPxActor* actor)
	{
		if (m_usePxUserData)
		{
			pxActor->userData = actor;
		}
		m_physXActorsMap[pxActor] = actor;
	}

	void					unregisterActor(PxRigidDynamic* pxActor)
	{
		if (m_usePxUserData)
		{
			pxActor->userData = nullptr;
		}
		m_physXActorsMap.erase(pxActor);
	}

	void					registerFamily(ExtPxFamily& family)
	{
		m_tkFamiliesMap[&family.getTkFamily()] = &family;
	}

	void					unregisterFamily(ExtPxFamily& family)
	{
		m_tkFamiliesMap.erase(&family.getTkFamily());
	}

	void					updateJoint(TkJoint& joint);


	//////// events dispatch ////////

	void					dispatchActorCreated(ExtPxFamily& family, ExtPxActor& actor)
	{
		for (ExtPxListener* listener : m_listeners)
			listener->onActorCreated(family, actor);
	}

	void					dispatchActorDestroyed(ExtPxFamily& family, ExtPxActor&actor)
	{
		for (ExtPxListener* listener : m_listeners)
			listener->onActorDestroyed(family, actor);
	}


private:

	//////// data ////////

	PxPhysics&												m_physics;
	TkFramework&											m_framework;
	ExtPxCreateJointFunction								m_createJointFn;
	bool													m_usePxUserData;
	InlineArray<ExtPxListener*, 8>::type					m_listeners;
	HashMap<const PxRigidDynamic*, ExtPxActor*>::type		m_physXActorsMap;
	HashMap<TkFamily*, ExtPxFamily*>::type					m_tkFamiliesMap;
	HashMap<TkActor*, Array<TkJoint*>::type >::type			m_incompleteJointMultiMap;
	uint32_t												m_actorCountLimit;
};

} // namespace Blast
} // namespace Nv


#endif // ifndef NVBLASTEXTPXMANAGERIMPL_H
