#include <common.h>
#if defined(CTR_NATIVE)
static int CollHost_Overlaps(const struct BoundingBox *a,const struct BoundingBox *b)
{
    return a->min.x<=b->max.x && b->min.x<=a->max.x &&
           a->min.y<=b->max.y && b->min.y<=a->max.y &&
           a->min.z<=b->max.z && b->min.z<=a->max.z;
}
void COLL_SearchBSP_CallbackPARAM(struct BSP *root,struct BoundingBox *bbox,CollBspLeafCallback callback,struct ScratchpadStruct *sps)
{
    if(root==NULL) return;
    if(bbox==NULL || callback==NULL || sps==NULL || sps->ptr_mesh_info==NULL ||
       sps->ptr_mesh_info->bspRoot!=root || sps->ptr_mesh_info->numBspNodes<=0 ||
       sps->ptr_mesh_info->numBspNodes>BSP_CHILD_ID_INDEX_MASK+1) CTR_TRAP();
    size_t capacity=(size_t)sps->ptr_mesh_info->numBspNodes;
    // Per-call traversal state allows leaf callbacks to start a nested search.
    BspChildId *stack=malloc(capacity*sizeof(*stack));
    u8 *seen=calloc(capacity,1);
    if(stack==NULL || seen==NULL) CTR_TRAP();
    struct BoundingBox bounds=*bbox;
    size_t count=0;
    struct BSP *node=root;
    seen[0]=1;
    for(;;) {
        for(unsigned i=0;i<2;i++) {
            u16 id=(u16)node->data.branch.childID[i];
            if(id==BSP_CHILD_ID_NONE) continue;
            size_t index=id&BSP_CHILD_ID_INDEX_MASK;
            if(index>=capacity) CTR_TRAP();
            if(!CollHost_Overlaps(&root[index].box,&bounds)) continue;
            if(count>=capacity || seen[index]) CTR_TRAP();
            seen[index]=1;
            stack[count++]=(BspChildId)id;
        }
        for(;;) {
            if(count==0) { free(seen); free(stack); return; }
            u16 id=(u16)stack[--count];
            node=&root[id&BSP_CHILD_ID_INDEX_MASK];
            if(id&BSP_CHILD_ID_LEAF_FLAG) callback(node,sps);
            else break;
        }
    }
}
#else
internal b32 COLL_SearchBSP_CallbackPARAM_Overlaps(struct BSP *node, const struct BoundingBox *bounds)
{
	return ((node->box.min.y <= bounds->max.y) && (node->box.min.x <= bounds->max.x) && (bounds->min.x <= node->box.max.x) &&
	        (node->box.min.z <= bounds->max.z) && (bounds->min.z <= node->box.max.z) && (bounds->min.y <= node->box.max.y));
}

internal void COLL_SearchBSP_CallbackPARAM_PushChild(struct BSP *root, BspChildId childID, const struct BoundingBox *bounds, BspChildId **stackTop)
{
	u16 rawChildID = (u16)childID;
	if (rawChildID == BSP_CHILD_ID_NONE)
	{
		return;
	}

	struct BSP *child = &root[rawChildID & BSP_CHILD_ID_INDEX_MASK];
	if (!COLL_SearchBSP_CallbackPARAM_Overlaps(child, bounds))
	{
		return;
	}

	**stackTop = childID;
	(*stackTop)++;
}

internal void COLL_SearchBSP_CallbackPARAM_PushChildren(struct BSP *root, struct BSP *node, const struct BoundingBox *bounds, BspChildId **stackTop)
{
	// Retail pushes child 0 then child 1; the scratchpad stack pops child 1 first.
	COLL_SearchBSP_CallbackPARAM_PushChild(root, node->data.branch.childID[0], bounds, stackTop);
	COLL_SearchBSP_CallbackPARAM_PushChild(root, node->data.branch.childID[1], bounds, stackTop);
}

void COLL_SearchBSP_CallbackPARAM(struct BSP *root, struct BoundingBox *bbox, CollBspLeafCallback callback, struct ScratchpadStruct *sps)
{
	if (root == NULL)
	{
		return;
	}

	struct BoundingBox bounds = *bbox;

	// Retail stores pending child IDs at scratchpad 0x1f800070 and pops them
	// LIFO, preserving the original BSP traversal order without host recursion.
	BspChildId *stackBase = CTR_SCRATCHPAD_PTR(BspChildId, 0x70);
	BspChildId *stackTop = stackBase;

	COLL_SearchBSP_CallbackPARAM_PushChildren(root, root, &bounds, &stackTop);

	while (stackTop != stackBase)
	{
		stackTop--;
		BspChildId childID = *stackTop;
		u16 rawChildID = (u16)childID;
		struct BSP *child = &root[rawChildID & BSP_CHILD_ID_INDEX_MASK];

		if ((rawChildID & BSP_CHILD_ID_LEAF_FLAG) != 0)
		{
			callback(child, sps);
			continue;
		}

		COLL_SearchBSP_CallbackPARAM_PushChildren(root, child, &bounds, &stackTop);
	}
}

#endif
