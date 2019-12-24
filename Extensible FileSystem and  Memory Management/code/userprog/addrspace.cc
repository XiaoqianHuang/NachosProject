// addrspace.cc 
//	Routines to manage address spaces (executing user programs).
//
//	In order to run a user program, you must:
//
//	1. link with the -n -T 0 option 
//	2. run coff2noff to convert the object file to Nachos format
//		(Nachos object code format is essentially just a simpler
//		version of the UNIX executable object code format)
//	3. load the NOFF file into the Nachos file system
//		(if you are using the "stub" file system, you
//		don't need to do this last step)
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "main.h"
#include "addrspace.h"
#include "machine.h"
#include "noff.h"

//----------------------------------------------------------------------
// SwapHeader
// 	Do little endian to big endian conversion on the bytes in the 
//	object file header, in case the file was generated on a little
//	endian machine, and we're now running on a big endian machine.
//----------------------------------------------------------------------

static void 
SwapHeader (NoffHeader *noffH)
{
    noffH->noffMagic = WordToHost(noffH->noffMagic);
    noffH->code.size = WordToHost(noffH->code.size);
    noffH->code.virtualAddr = WordToHost(noffH->code.virtualAddr);
    noffH->code.inFileAddr = WordToHost(noffH->code.inFileAddr);
#ifdef RDATA
    noffH->readonlyData.size = WordToHost(noffH->readonlyData.size);
    noffH->readonlyData.virtualAddr = 
           WordToHost(noffH->readonlyData.virtualAddr);
    noffH->readonlyData.inFileAddr = 
           WordToHost(noffH->readonlyData.inFileAddr);
#endif 
    noffH->initData.size = WordToHost(noffH->initData.size);
    noffH->initData.virtualAddr = WordToHost(noffH->initData.virtualAddr);
    noffH->initData.inFileAddr = WordToHost(noffH->initData.inFileAddr);
    noffH->uninitData.size = WordToHost(noffH->uninitData.size);
    noffH->uninitData.virtualAddr = WordToHost(noffH->uninitData.virtualAddr);
    noffH->uninitData.inFileAddr = WordToHost(noffH->uninitData.inFileAddr);

#ifdef RDATA
    DEBUG(dbgAddr, "code = " << noffH->code.size <<  
                   " readonly = " << noffH->readonlyData.size <<
                   " init = " << noffH->initData.size <<
                   " uninit = " << noffH->uninitData.size << "\n");
#endif
}

//----------------------------------------------------------------------
// AddrSpace::AddrSpace
// 	Create an address space to run a user program.
//	Set up the translation from program memory to physical 
//	memory.  For now, this is really simple (1:1), since we are
//	only uniprogramming, and we have a single unsegmented page table
//----------------------------------------------------------------------

AddrSpace::AddrSpace()
{
  currentDirSector = 1;
  currentDir = kernel->fileSystem->getFullName(currentDirSector);
}

//----------------------------------------------------------------------
// AddrSpace::~AddrSpace
// 	Dealloate an address space.
//----------------------------------------------------------------------

AddrSpace::~AddrSpace()
{
  for (int i = 0; i < numPages; i++) {
    if (pageTable[i].physicalPage != -1) {
      kernel->freeMap->Clear(pageTable[i].physicalPage); // free the memory
      kernel->stats->memRefNum = kernel->stats->memRefNum - PageSize;
      DEBUG(dbgSys, "Free the space. Memory Referrence Num:" << kernel->stats->memRefNum);
      if (kernel->useTLB == TRUE) {
        kernel->machine->DeleteTLB(&pageTable[i]);
      }
    }
  }
   delete pageTable;
   delete openFileTable;
}


//----------------------------------------------------------------------
// AddrSpace::Load
// 	Load a user program into memory from a file.
//
//	Assumes that the page table has been initialized, and that
//	the object code file is in NOFF format.
//
//	"fileName" is the file containing the object code to load into memory
//----------------------------------------------------------------------

bool 
AddrSpace::Load(char *fileName) 
{  
    OpenFile *executable = kernel->fileSystem->Open(fileName);
    openFileTable = new map<string, int>();
   // (*openFileTable)[string(kernel->consoleIn)] = 0; // stdin
   // (*openFileTable)[string(kernel->consoleOut)] = 1; // stdout

    NoffHeader noffH;
    unsigned int size;

    if (executable == NULL) {
	cerr << "Unable to open file " << fileName << "\n";
	return FALSE;
    } 
    
    executable->ReadAt((char *)&noffH, sizeof(noffH), 0);
    if ((noffH.noffMagic != NOFFMAGIC) && 
		(WordToHost(noffH.noffMagic) == NOFFMAGIC))
    	SwapHeader(&noffH);
    ASSERT(noffH.noffMagic == NOFFMAGIC);

#ifdef RDATA
// how big is address space?
    size = noffH.code.size + noffH.readonlyData.size + noffH.initData.size +
           noffH.uninitData.size + UserStackSize;	
                                                // we need to increase the size
						// to leave room for the stack
#else
// how big is address space?
    size = noffH.code.size + noffH.initData.size + noffH.uninitData.size 
			+ UserStackSize;	// we need to increase the size
						// to leave room for the stack
#endif
    numPages = divRoundUp(size, PageSize);
    size = numPages * PageSize;

    //ASSERT(numPages <= NumPhysPages);		// check we're not trying
						// to run anything too big --
						// at least until we have
						// virtual memory

    DEBUG(dbgAddr, "Initializing address space: " << numPages << ", " << size);

// then, copy in the code and data segments into memory

    pageTable = new TranslationEntry[numPages];
    for (int i = 0; i < numPages; i++) {
      pageTable[i].virtualPage = kernel->swapCounter;	// virt page #  = swap_Counter
      pageTable[i].physicalPage = -1;
      pageTable[i].valid = FALSE;
      pageTable[i].use = FALSE;
      pageTable[i].dirty = FALSE;
      pageTable[i].readOnly = FALSE;

      char* buffer = new char[PageSize];
      executable->ReadAt(buffer, PageSize, noffH.code.inFileAddr+(PageSize*i)); // read from file by page size // when write back, read header to fetch the addr again
      kernel->swapSpace->WriteAt(buffer, PageSize, kernel->swapCounter*PageSize); // write to swapspace
      kernel->swapCounter++;
    }

#ifdef RDATA
    if (noffH.readonlyData.size > 0) {
        DEBUG(dbgAddr, "Initializing read only data segment.");
	DEBUG(dbgAddr, noffH.readonlyData.virtualAddr << ", " << noffH.readonlyData.size);
        executable->ReadAt(
		&(kernel->machine->mainMemory[noffH.readonlyData.virtualAddr]),
			noffH.readonlyData.size, noffH.readonlyData.inFileAddr);
    }
#endif

    delete executable;			// close file
    return TRUE;			// success
}

//----------------------------------------------------------------------
// AddrSpace::Execute
// 	Run a user program using the current thread
//
//      The program is assumed to have already been loaded into
//      the address space
//
//----------------------------------------------------------------------

void 
AddrSpace::Execute() 
{

    kernel->currentThread->space = this;

    this->InitRegisters();		// set the initial register values
    this->RestoreState();		// load page table register

    kernel->machine->Run();		// jump to the user progam

    ASSERTNOTREACHED();			// machine->Run never returns;
					// the address space exits
					// by doing the syscall "exit"
}


//----------------------------------------------------------------------
// AddrSpace::InitRegisters
// 	Set the initial values for the user-level register set.
//
// 	We write these directly into the "machine" registers, so
//	that we can immediately jump to user code.  Note that these
//	will be saved/restored into the currentThread->userRegisters
//	when this thread is context switched out.
//----------------------------------------------------------------------

void
AddrSpace::InitRegisters()
{
    Machine *machine = kernel->machine;
    int i;

    for (i = 0; i < NumTotalRegs; i++)
	machine->WriteRegister(i, 0);

    // Initial program counter -- must be location of "Start", which
    //  is assumed to be virtual address zero
    machine->WriteRegister(PCReg, 0);	

    // Need to also tell MIPS where next instruction is, because
    // of branch delay possibility
    // Since instructions occupy four bytes each, the next instruction
    // after start will be at virtual address four.
    machine->WriteRegister(NextPCReg, 4);

   // Set the stack register to the end of the address space, where we
   // allocated the stack; but subtract off a bit, to make sure we don't
   // accidentally reference off the end!
    machine->WriteRegister(StackReg, numPages * PageSize - 16);
    DEBUG(dbgAddr, "Initializing stack pointer: " << numPages * PageSize - 16);
}

//----------------------------------------------------------------------
// AddrSpace::SaveState
// 	On a context switch, save any machine state, specific
//	to this address space, that needs saving.
//
//	For now, don't need to save anything!
//----------------------------------------------------------------------

void AddrSpace::SaveState() 
{}

//----------------------------------------------------------------------
// AddrSpace::RestoreState
// 	On a context switch, restore the machine state so that
//	this address space can run.
//
//      For now, tell the machine where to find the page table.
//----------------------------------------------------------------------

void AddrSpace::RestoreState() 
{
    kernel->machine->pageTable = pageTable;
    kernel->machine->pageTableSize = numPages;
}


//----------------------------------------------------------------------
// AddrSpace::Translate
//  Translate the virtual address in _vaddr_ to a physical address
//  and store the physical address in _paddr_.
//  The flag _isReadWrite_ is false (0) for read-only access; true (1)
//  for read-write access.
//  Return any exceptions caused by the address translation.
//----------------------------------------------------------------------
ExceptionType
AddrSpace::Translate(unsigned int vaddr, unsigned int *paddr, int isReadWrite) //
{
    TranslationEntry *pte;
    int pfn;
    unsigned int      vpn    = vaddr / PageSize;
    unsigned int      offset = vaddr % PageSize;

    //if(vpn >= numPages) {
    //    return AddressErrorException;
    //}

    // check TLB first

    // if not in TLB, check for all pte
    bool isInMem = FALSE;

    for (int i = 0; i < numPages; i++) {
      if (pageTable[i].virtualPage == vpn && pageTable[i].valid == TRUE) {
        isInMem = TRUE;
      }
    }

    if (isInMem == TRUE) {
      pte = &pageTable[vpn];
      if (isReadWrite && pte->readOnly) {
        return ReadOnlyException;
      }

      pfn = pte->physicalPage;
      pte->use = TRUE;          // set the use, dirty bits

      if (isReadWrite)
        pte->dirty = TRUE;

      *paddr = pte->physicalPage + offset;
      cout << "PHYMEM:" << *paddr << "\n";
     ASSERT((*paddr >= 0) && ((*paddr) <= MemorySize));
     DEBUG(dbgAddr, "phys addr = " << *paddr);
      ASSERT((*paddr < MemorySize));

      //cerr << " -- AddrSpace::Translate(): vaddr: " << vaddr <<
      //  ", paddr: " << *paddr << "\n";

      return NoException;
    }
    else {
      return PageFaultException;
    }

    // if the pageFrame is too big, there is something really wrong!
    // An invalid translation was loaded into the page table or TLB.
    if (pfn >= NumPhysPages) {
        DEBUG(dbgAddr, "Illegal physical page " << pfn);
        return BusErrorException;
    }
}

TranslationEntry* 
AddrSpace::getPageEntry(int pageFaultPhysicalNum) {
  return &pageTable[pageFaultPhysicalNum]; // return pointer of the required entry.
}

AddrSpace::AddrSpace(const AddrSpace& copiedItem) { // copy constructor
  numPages = copiedItem.numPages;
  pageTable = new TranslationEntry[numPages];
  for (int i = 0; i < numPages; i++) {
      pageTable[i].virtualPage = copiedItem.pageTable[i].virtualPage;
      pageTable[i].physicalPage = copiedItem.pageTable[i].physicalPage;
      pageTable[i].valid = copiedItem.pageTable[i].valid;
      pageTable[i].use = copiedItem.pageTable[i].use;
      pageTable[i].dirty = copiedItem.pageTable[i].dirty;
      pageTable[i].readOnly = copiedItem.pageTable[i].readOnly;
  }
  currentDirSector = copiedItem.currentDirSector;
  currentDir = copiedItem.currentDir;

  openFileTable = new map<string, int>();
  map<string, int>::iterator iter;
  iter = (*copiedItem.openFileTable).begin();
  while (iter != (*copiedItem.openFileTable).end()) {
    (*openFileTable)[iter->first] = iter->second; // copy
    (*kernel->openNumForWR)[iter->first]++; // increase the count
    iter++;
  }
}

int 
AddrSpace::getNumPage() {
  return numPages;
}


TranslationEntry* 
AddrSpace::getPageTable() {
  return pageTable;
}

bool 
AddrSpace::isExisted(int id) {
  map<string, int>::iterator iter;
  iter = openFileTable->begin();
  while (iter != openFileTable->end()) {
    if (iter->second == id) { //  find the pair
      return TRUE;
    }
    iter++;
  }
  return FALSE;
}

void 
AddrSpace::removeById(int id) {
  map<string, int>::iterator iter;
  iter = openFileTable->begin();
  while (iter != openFileTable->end()) {
    if (iter->second == id) { //  find the pair
      openFileTable->erase(iter);
      break;
    }
    iter++;
  }
}