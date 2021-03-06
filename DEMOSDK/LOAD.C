/*------------------------------------------------------------------------------  -----------------
  The MIT License (MIT)

  Copyright (c) 2015-2017 J.Hubert

  Permission is hereby granted, free of charge, to any person obtaining a copy of this software 
  and associated documentation files (the "Software"), 
  to deal in the Software without restriction, including without limitation the rights to use, 
  copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, 
  and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all copies 
  or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, 
  INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, 
  DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------------------------- */

#define LOAD_C

#include "DEMOSDK\BASTYPES.H"
#include "DEMOSDK\LOAD.H"
#include "DEMOSDK\HARDWARE.H"
#include "DEMOSDK\STANDARD.H"
#include "DEMOSDK\SYSTEM.H"
#include "DEMOSDK\ALLOC.H"

u16 LOADorder = 1;

ASMIMPORT volatile u8				LOADinprogress;
ASMIMPORT volatile u16				LOADsecpertrack;
ASMIMPORT		   LOADrequest		LOADreq;
ASMIMPORT volatile LOADrequest* 	LOADcurrentreq;
ASMIMPORT          u16				LOADreqNb;
ASMIMPORT volatile u32              LOADdeconnectAndStartRequest;

LOADrequest* LOADpush (void* _buffer, u16 _startsector, u32 _starttracknside, u32 _loadnbsectorsnorder);
void LOADidle(void);

#define LOAD_SECTORS_PER_TRACK 10

#ifdef DEMOS_DEBUG
/* #	define LOAD_CHECK_CONTENT */ /* LOAD_CHECK_CONTENT needs DEMOS_DEBUG */
#endif


void LOADinit (LOADdisk* _firstmedia, u16 _nbEntries, u16 _nbMetaData)
{
#	ifdef __TOS__
	*HW_VECTOR_DMA = (u32) LOADidle;
	LOADsecpertrack = LOAD_SECTORS_PER_TRACK;
	STDcpuSetSR(0x2300);
#	endif

    LOADinitFAT (0, _firstmedia, _nbEntries, _nbMetaData);
}


#ifdef DEMOS_LOAD_FROMHD
void LOADpreloadMedia (LOADdisk* _media)
{
    FILE* file = fopen(_media->filename, "rb");
    u32 result;
    u32 size = _media->mediapreloadsize - LOAD_SECTORSIZE; /* skip bootsector */


    ASSERT(file != NULL);

    _media->mediapreload = malloc(size);
    ASSERT(_media->mediapreload != NULL);

    fseek(file, LOAD_SECTORSIZE, SEEK_SET); /* skip bootsector */
    result = fread (_media->mediapreload, 1, size, file);
    ASSERT(result == size);

    fclose(file);
}
#endif


void LOADinitFAT (u8 _drive, LOADdisk* _media, u16 _nbEntries, u16 _nbMetaData)
{
    u16* temp;

#   ifdef DEMOS_LOAD_FROMHD
    temp = (u16*) _media->mediapreload;
#   else
    temp = (u16*) RINGallocatorAlloc (&sys.mem, LOAD_SECTORSIZE * LOAD_FAT_NBSECTORS);

    _drive = sys.has2Drives & (_drive != DEMOS_INVERT_DRIVE);

    {
        LOADrequest* loadRequest = LOADpush (temp, LOAD_FAT_STARTSECTOR + 1, ((u32)_drive << 17), ((u32)LOAD_PRIOTITY_HIGH << 16) | LOAD_FAT_NBSECTORS);
        LOADwaitRequestCompleted (loadRequest);
    }
#	endif

    _media->nbEntries  = PCENDIANSWAP16(temp[0]);
    _media->nbMetaData = PCENDIANSWAP16(temp[1]);

    ASSERT(_media->nbEntries == _nbEntries);
    ASSERT(_media->nbMetaData == _nbMetaData);

    {
        u16 entriessize  = _media->nbEntries  * sizeof(LOADresource);
        u16 metadatasize = _media->nbMetaData * sizeof(LOADmetadata);
        u16 preloadsize  = _media->nbEntries  * sizeof(void*);
        
        u8* FATbuffer = (u8*) RINGallocatorAlloc (&sys.coremem, entriessize + metadatasize + preloadsize);
        ASSERT(FATbuffer != NULL);

        _media->FAT      = (LOADresource*) FATbuffer;
        _media->metaData = (LOADmetadata*) (FATbuffer + entriessize);
        _media->preload  = (void**) (FATbuffer + entriessize + metadatasize);

        STDmcpy (FATbuffer, &temp[2], entriessize + metadatasize);
        STDmset (_media->preload, 0, preloadsize);
    }

#   ifndef __TOS__
    {
        u16 t;

        for (t = 0 ; t < _media->nbEntries ; t++)
        {
            _media->FAT[t].startsectorsidenbsectors = PCENDIANSWAP16(_media->FAT[t].startsectorsidenbsectors);
            _media->FAT[t].metadataindextrack       = PCENDIANSWAP16(_media->FAT[t].metadataindextrack);
        }

        for (t = 0 ; t < _media->nbMetaData ; t++)
        {
            _media->metaData[t].offsetsizeh       = PCENDIANSWAP32(_media->metaData[t].offsetsizeh);
            _media->metaData[t].originalsizesizel = PCENDIANSWAP32(_media->metaData[t].originalsizesizel);
        }
    }
#   endif

#   ifdef DEMOS_LOAD_FROMHD
    {
        u8* p = (u8*) _media->mediapreload;
        u16 t;

        p += LOAD_FAT_NBSECTORS * LOAD_SECTORSIZE;

        for (t = 1 ; t < _media->nbEntries ; t++)
        {
            u16 nbsectors = _media->FAT[t].startsectorsidenbsectors & LOAD_RESOURCE_MASK_NBSECTORS;

            _media->preload[t] = p;

            p += nbsectors * LOAD_SECTORSIZE;
        }
    }
#   else
    RINGallocatorFree (&sys.mem, temp);
#   endif
}

#ifdef DEMOS_LOAD_FROMHD

LOADrequest* LOADrequestLoad (LOADdisk* _media, u16 _resourceid, void* _buffer, u16 _order)
{
    ASSERT(0);
    return NULL;
}

void* LOADpreload (void* _framebuffer, u16 _pitch, u16 _planepitch, void* _preload, u32 _preloadsize, void* _current, LOADdisk* _disk, u8* _resources, u16 _nbResources)
{
    ASSERT(0);
    return NULL;
}

#else

LOADrequest* LOADrequestLoad (LOADdisk* _media, u16 _resourceid, void* _buffer, u16 _order)
{
	LOADresource* rsc = &_media->FAT[_resourceid];

    u32 track       = rsc->metadataindextrack & LOAD_RESOURCE_MASK_TRACK;
    u32 side        = (rsc->startsectorsidenbsectors & LOAD_RESOURCE_MASK_SIDE) != 0;
    u16 startsector = (rsc->startsectorsidenbsectors >> LOAD_RESOURCE_RSHIFT_STARTSECTOR) & LOAD_RESOURCE_MASK_STARTSECTOR;
    u32 nbsectors   = rsc->startsectorsidenbsectors & LOAD_RESOURCE_MASK_NBSECTORS;

    u8 drive        = sys.has2Drives & (_media->preferedDrive != DEMOS_INVERT_DRIVE);
    
	LOADrequest* loadRequest = LOADpush (_buffer, startsector + 1, track | (side << 16) | ((u32) drive << 17), ((u32)_order << 16) | nbsectors);

#	ifdef LOAD_CHECK_CONTENT
	if ( loadRequest != NULL )
	{
		u16* image = (u16*)(((u32)*HW_VIDEO_BASE_H << 16) | ((u32)*HW_VIDEO_BASE_M << 8) | ((u32)*HW_VIDEO_BASE_L));
		static y = 40;
		u32 i;
        u8* buf;
		u32 size = rsc->size;


        LOADwaitRequestCompleted (loadRequest);
        LOADwaitFDCIdle();

        buf = (u8*) loadUsingStdlib (NULL, _media, track, side, startsector, nbsectors);

		for (i = 0 ; i < size ; i++)
		{
            if ( *(buf + i) != *(((u8*)_buffer) + i) )
			{
                u16* image = (u16*)(((u32)*HW_VIDEO_BASE_H << 16) | ((u32)*HW_VIDEO_BASE_M << 8) | ((u32)*HW_VIDEO_BASE_L));
                static char line[] = "            ";

                STDuxtoa(line, i, 6);
                SYSdebugPrint ( image + 2, 160, SYS_4P_BITSHIFT, 0, 100, line);
                break;
			}
		}

		free (buf);
	}
#	endif /* LOAD_CHECK_CONTENT */

    return loadRequest;
}

void* LOADpreload (void* _framebuffer, u16 _pitch, u16 _planepitch, void* _preload, u32 _preloadsize, void* _current, LOADdisk* _disk, u8* _resources, u16 _nbResources)
{
    static char line[] = "preloading    -      ";
    u8* currentpreload = (u8*) _current;
    u16 t;


    for (t = 0 ; t < _nbResources ; t++)
    {
        u16 rsc     = _resources[t];
        u32 rscSize = LOADresourceRoundedSize(_disk, rsc);

        if ( ( _preloadsize - (currentpreload - (u8*)_preload) ) >= rscSize )
	    {
            LOADrequest* request = LOADrequestLoad (_disk, rsc, currentpreload, LOAD_PRIORITY_INORDER);

            _disk->preload[rsc] = currentpreload;

            while (request->processed == false)
            {
                STDuxtoa(&line[11], _nbResources - t - 1, 2);
                STDuxtoa(&line[16], request->nbsectors, 4);

                SYSfastPrint (line, _framebuffer, _pitch, _planepitch);
	        }

            LOADfreeRequest (request);
            currentpreload += rscSize;
        }
    }

    return currentpreload;
}

#endif


LOADrequest* LOADdata (LOADdisk* _media, u16 _resource, void* _buffer, u16 _order)
{
    if ( _media->preload[_resource] != NULL )
    {
        STDmcpy(_buffer, _media->preload[_resource], LOADresourceRoundedSize(_media, _resource) );
    }
#   ifndef DEMOS_LOAD_FROMHD
    else
    {
        return LOADrequestLoad (_media, _resource, _buffer, _order);
    }
#   endif

    return NULL;
}


u32 LOADresourceRoundedSize (LOADdisk* _media, u16 _entryIndex)
{
    ASSERT(_entryIndex < _media->nbEntries);
    return (u32)(_media->FAT[_entryIndex].startsectorsidenbsectors & LOAD_RESOURCE_MASK_NBSECTORS) * LOAD_SECTORSIZE;
}

u16 LOADresourceMetaDataIndex (LOADdisk* _media, u16 _entryIndex)
{
    ASSERT(_entryIndex < _media->nbEntries);
    return _media->FAT[_entryIndex].metadataindextrack >> LOAD_RESOURCE_RSHIFT_METADATA;
}

u32 LOADmetadataOffset (LOADdisk* _media, u16 _metaDataIndex)
{
    ASSERT(_metaDataIndex < _media->nbMetaData);
    return _media->metaData[_metaDataIndex].offsetsizeh >> LOAD_METADATA_RSHIFT_OFFSET;
}

u32 LOADmetadataSize (LOADdisk* _media, u16 _metaDataIndex)
{
    u32 size;

    ASSERT(_metaDataIndex < _media->nbMetaData);

    size   = _media->metaData[_metaDataIndex].offsetsizeh & LOAD_METADATA_MASK_SIZEH;
    size <<= LOAD_METADATA_LSHIFT_SIZEH;
    size  |= _media->metaData[_metaDataIndex].originalsizesizel & LOAD_METADATA_MASK_SIZEL;

    return size;
}

u32 LOADmetadataOriginalSize (LOADdisk* _media, u16 _metaDataIndex)
{
    ASSERT(_metaDataIndex < _media->nbMetaData);
    return _media->metaData[_metaDataIndex].originalsizesizel >> LOAD_METADATA_RSHIFT_ORIGINALSIZE;
}

void LOADwaitFDCIdle (void)
{
	while ( LOADinprogress );
}

void LOADwaitRequestCompleted (LOADrequest* _request)
{
    if ( _request != NULL )
    {
        ASSERT (_request->allocated);
	    while ( _request->processed == 0 );
	    _request->allocated = false;
    }
}


#ifdef DEMOS_DEBUG
u16 LOADtrace (void* _image, u16 _pitch, u16 _planePitch, u16 _y)
{
	u16 t, h = 8;

	{
	    static char line[] = "FDCbusy=   request=       desel=       ";

		line[8] = '0' + (LOADinprogress != 0);
		STDuxtoa(&line[19], (u32) LOADcurrentreq, 6);
        STDuxtoa(&line[32], LOADdeconnectAndStartRequest, 6);

		SYSdebugPrint ( _image, _pitch, _planePitch, 0, _y, line);
	}
	
	{
		static char line[] = "            ";
		LOADrequest* r = &LOADreq;		

		for (t = 0 ; t < LOADreqNb ; t++, r++)
		{
            line[0] = ( r == LOADcurrentreq ) ? '>' : ' ';

			if ( r->allocated )
			{
                if ( r->processed == false )
                {
					line[2] = '*';
				    STDuxtoa(&line[8], r->nbsectors, 3);
			    }
				else
				{
					line[2] = '+';
				}
				STDuxtoa(&line[4], r->order		, 3);
			}
			else
			{
				STDmset(line, 0x20202020UL, sizeof(line) - 1);
				line[2] = '-';
			}

    		SYSdebugPrint ( _image, _pitch, _planePitch, 0, _y + h, line);
            h += 8;
		}
	}

    if (LOADcurrentreq != NULL)
	{
		static char line[] = "tk=   sc=   si=  dv=  nb=    a=      ";

		STDuxtoa(&line[3], LOADcurrentreq->track, 2);
		STDuxtoa(&line[9], LOADcurrentreq->sector, 2);
		line[15] = '0' + ((LOADcurrentreq->side_drive & 0x0100) == 0);
		line[20] = '0' + ((LOADcurrentreq->side_drive & 0x0400) == 0);
		STDuxtoa(&line[25], LOADcurrentreq->nbsectors, 3);
		STDuxtoa(&line[31], (u32) LOADcurrentreq->address, 6);

		SYSdebugPrint ( _image, _pitch, _planePitch, 0, _y + h, line);
		h += 8;
	}

	return h;
}
#endif


#if defined(DEMOS_UNITTEST) && !defined(DEMOS_LOAD_FROMHD)

#define NBREQUESTS 2
#define RESOURCEOFFSET 6

u8*				g_bufReference[NBREQUESTS];
u8*				g_bufLoaded[NBREQUESTS];
LOADrequest*	g_requests[NBREQUESTS];
LOADdisk*		g_media = NULL;

/* wrong dependency => for test only */
/* #include "REBIRTH\DISK2.H" */

void LOADunitTestInit (FSM* _fsm)
{
	u16 t;


    IGNORE_PARAM(_fsm);
    
    /*g_media = &RSC_DISK2;*/

	for (t = 0 ; t < NBREQUESTS ; t++)
	{
#       ifndef DEMOS_USES_BOOTSECTOR
        g_bufReference [t] = (u8*) g_media->preload[t + RESOURCEOFFSET];
#       endif

        g_bufLoaded [t] = (u8*) MEM_ALLOC (&sys.allocatorMem, LOAD_SECTORSIZE * 255UL);
		
		g_requests [t] = NULL;
	}

    FSMgotoNextState (_fsm);
}

void LOADunitTestUpdate (FSM* _fsm)
{
	u16 num = STDmfrnd();
	u16 t;
	

    IGNORE_PARAM(_fsm);

	if ((num & 0x1FFF) == 16)
	{
		for (t = 0 ; t < NBREQUESTS ; t++)
		{
			if ( g_requests[t] == NULL )
			{
				g_requests[t] = LOADrequestLoad(g_media, t + RESOURCEOFFSET, g_bufLoaded[t], (STDmfrnd() & 7) == 0 ? LOAD_PRIOTITY_HIGH : LOAD_PRIORITY_INORDER);
			}
		}
	}

	for (t = 0 ; t < NBREQUESTS ; t++)
	{
		if ( g_requests[t] != NULL )
		{
			if ( g_requests[t]->processed )
			{
				u32 size = LOADresourceRoundedSize (g_media, t + RESOURCEOFFSET);
				u32 i;

#               ifndef DEMOS_USES_BOOTSECTOR
				for (i = 0 ; i < size ; i++)
				{
					if ( *(g_bufReference[t] + i) != *(g_bufLoaded[t] + i) )
					{
						break;
					}
				}

				if ( i < size )
				{
					u16* image = (u16*)(((u32)*HW_VIDEO_BASE_H << 16) | ((u32)*HW_VIDEO_BASE_M << 8) | ((u32)*HW_VIDEO_BASE_L));
					static char line[] = "            ";

					STDuxtoa(line, i, 6);
					SYSdebugPrint ( image, 160, SYS_4P_BITSHIFT, 0, 100, line);

                    ASSERT(0);
				}
#               endif

				g_requests[t]->allocated = false;
				g_requests[t] = NULL;
			}
		}
	}
}

#endif
