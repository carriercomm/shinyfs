#pragma once
#ifndef SHINYMETAFILE_H
#define SHINYMETAFILE_H
#include <sys/types.h>
#include "ShinyMetaNode.h"
#include <cryptopp/cryptlib.h>
#include <cryptopp/whrlpool.h>
#include <cryptopp/sha.h>

using namespace CryptoPP;

//This is a hash of the entire file
typedef Whirlpool Filehasher;
typedef unsigned char Filehash[Filehasher::DIGESTSIZE];

//This is a hash of a small chunk of a file
typedef SHA256 Chunkhasher;
typedef unsigned char Chunkhash[Chunkhasher::DIGESTSIZE];

class ShinyMetaDir;
class ShinyMetaFile : public ShinyMetaNode {
public:
    //Create a new one
    ShinyMetaFile( ShinyMetaFilesystem * fs, const char * newName );
    
    //Same as above, but adds this guy as a child to given parent (this is just for convenience, this just calls "addNode()" for you)
    ShinyMetaFile( ShinyMetaFilesystem * fs, const char * newName, ShinyMetaDir * parent );
    
    ShinyMetaFile( const char * serializedInput, ShinyMetaFilesystem * fs );
    
    //Set a new length for this file (Is this really how it should work?)
    virtual void setFileLen( uint64_t newLen );
    virtual uint64_t getFileLen();
    
    //Performs various checks to make sure this node is all right
    virtual bool sanityCheck();
    
    //Get the length of 
    virtual uint64_t serializedLen( void );
    virtual void serialize( char * output );
    virtual ShinyNodeType getNodeType( void );
protected:
    virtual void unserialize( const char * input );
private:
    uint64_t filelen;                   //Length of the file

    Filehash hash;                      //Hash of the whole file
};

#endif //SHINYMETAFILE_H