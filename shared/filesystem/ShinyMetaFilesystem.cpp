#include "ShinyMetaFilesystem.h"
#include "ShinyMetaFile.h"
#include "ShinyMetaDir.h"
#include "ShinyMetaRootDir.h"
#include <base/Logger.h>

//Used to stat() to tell if the directory exists
#include <sys/stat.h>
#include <sys/fcntl.h>

ShinyMetaFilesystem::ShinyMetaFilesystem( const char * serializedData, const char * filecache ) : nextInode(1), root(NULL) {
    //Check if the filecache exists
    struct stat st;
    if( stat( filecache, &st ) != 0 ) {
        WARN( "filecache %s does not exist, creating a new one!", filecache );
        mkdir( filecache, 0x1ff );
    }
    
    //Finally, copy it in so we know where to find it!
    this->filecache = new char[strlen(filecache)+1];
    strcpy( this->filecache, filecache );
    this->fscache = new char[strlen(filecache)+1+8+1];
    sprintf( this->fscache, "%s/fs.cache", this->filecache );

    
    if( serializedData && serializedData[0] ) {
        unserialize( serializedData );
    } else {
        //Let's look in the filecache
        
        if( stat( this->fscache, &st ) != 0 ) {
            WARN( "fscache %s does not exist, starting over....", this->fscache );
            root = new ShinyMetaRootDir(this);
        } else {
            char * loadedSerializedData = NULL;
            ERROR( "load in the serialized data here!" );
            ERROR( "load in the serialized data here!" );
            ERROR( "load in the serialized data here!" );
            ERROR( "load in the serialized data here!" );
            ERROR( "load in the serialized data here!" );
            ERROR( "load in the serialized data here!" );
        }
    }
}

ShinyMetaFilesystem::~ShinyMetaFilesystem() {
    //Clear out the nodes (remember that when a node dies, it automagically removes itself from the fs, just like it adds itself)
    while( !this->nodes.empty() )
        delete( this->nodes.begin()->second );
}

#define min( x, y ) ((x) < (y) ? (x) : (y))
//Searches a ShinyMetaDir's listing for a name, returning the child
ShinyMetaNode * ShinyMetaFilesystem::findMatchingChild( ShinyMetaDir * parent, const char * childName, uint64_t childNameLen ) {
    const std::list<inode_t> * list = parent->getListing();
    for( std::list<inode_t>::const_iterator itty = list->begin(); itty != list->end(); ++itty ) {
        ShinyMetaNode * sNode = findNode( (*itty) );
        
        //Compare sNode's filename with the section of path inbetween
        if( memcmp( sNode->getName(), childName, min( strlen(sNode->getName()), childNameLen ) ) == 0 ) {
            //If it works, then we return sNode
            return sNode;
        }
    }
    //If we made it all the way through without finding a match for that file, quit out
    return NULL;
}

ShinyMetaNode * ShinyMetaFilesystem::findNode( const char * path ) {
    if( path[0] != '/' ) {
        ERROR( "path %s is unacceptable, must be an absolute path!", path );
        return NULL;
    }
    
    ShinyMetaNode * currNode = root;
    unsigned long filenameBegin = 1;
    for( unsigned int i=1; i<strlen(path); ++i ) {
        if( path[i] == '/' ) {
            //If this one actually _is_ a directory, let's get its listing
            if( currNode->getNodeType() == SHINY_NODE_TYPE_DIR || currNode->getNodeType() == SHINY_NODE_TYPE_ROOTDIR ) {
                //Search currNode's children for a name match
                ShinyMetaNode * childNode = findMatchingChild( (ShinyMetaDir *)currNode, &path[filenameBegin], i - filenameBegin );
                if( !childNode ) {
//                    ERROR( "Cannot resolve path %s, as the node %s does not have next node inside of it", path, currNode->getName() );
                    return NULL;
                } else {
                    currNode = childNode;
                    filenameBegin = i+1;
                }
            } else {
//                ERROR("Cannot resolve path %s, as the node %s is not a directory", path, currNode->getName() );
                return NULL;
            }
            
            //Update the beginning of the next filename to be the character after this slash
            filenameBegin = i+1;
        }
    }
    if( filenameBegin < strlen(path) ) {
        ShinyMetaNode * childNode = findMatchingChild( (ShinyMetaDir *)currNode, &path[filenameBegin], strlen(path) - filenameBegin );
        if( !childNode ) {
//            ERROR( "Cannot resolve path %s, as the node %s does not have next node inside of it", path, currNode->getName() );
            return NULL;
        } else
            currNode = childNode;
    }
    return currNode;
}

ShinyMetaNode * ShinyMetaFilesystem::findNode( uint64_t inode ) {
    std::tr1::unordered_map<inode_t, ShinyMetaNode *>::iterator itty = this->nodes.find( inode );
    if( itty != this->nodes.end() )
        return (*itty).second;
    return NULL;
}

ShinyMetaDir * ShinyMetaFilesystem::findParentNode( const char *path ) {
    uint64_t len = strlen( path );
    uint64_t end = len-1;
    if( path[len-1] == '/' )
        end--;
    
    while( end > 1 && path[end] != '/' )
        end--;
    
    char * newPath = new char[len - end + 1];
    memcpy( newPath, path, len - end );
    newPath[len - end] = 0;
    
    ShinyMetaDir * ret = (ShinyMetaDir *)this->findNode( newPath );
    delete( newPath );
    return ret;
}

void ShinyMetaFilesystem::setDirty( void ) {
    this->dirty = true;
}

bool ShinyMetaFilesystem::isDirty( void ) {
    return this->dirty;
}

bool ShinyMetaFilesystem::sanityCheck( void ) {
    bool retVal = true;
    //Call sanity check on all of them.
    //The nodes will return false if there is an error they cannot correct (right now that cannot happen)
    for( std::tr1::unordered_map<inode_t, ShinyMetaNode *>::iterator itty = this->nodes.begin(); itty != this->nodes.end(); ++itty ) {
        if( (*itty).second ) {
            if( !(*itty).second->sanityCheck() )
                retVal = false;
        } else {
            WARN( "inode [%d] is not in the nodes map!" );
            this->nodes.erase( itty++ );
        }
    }
    return retVal;
}

uint64_t ShinyMetaFilesystem::serialize( char ** store ) {
    //First, we're going to find the total length of this serialized monstrosity
    //We'll start with the version number
    uint64_t len = sizeof(uint16_t);
    
    //Next, what ShinyMetaFilesystem takes up itself
    //       nextInode        numNodes
    len += sizeof(inode_t) + sizeof(uint64_t);
    
    for( std::tr1::unordered_map<inode_t, ShinyMetaNode *>::iterator itty = nodes.begin(); itty != nodes.end(); ++itty ) {
        //Add an extra uint8_t for the node type
        len += (*itty).second->serializedLen() + sizeof(uint8_t);
    }
    
    //Now, reserve buffer space
    char * output = new char[len];
    *store = output;
    
    //Now, actually write into that buffer space
    //First, version number
    *((uint16_t *)output) = ShinyMetaFilesystem::VERSION;
    output += sizeof(uint16_t);
    
    //Next, nextInode, followed by the number of nodes we're writing out
    *((inode_t *)output) = this->nextInode;
    output += sizeof(inode_t);
    
    *((uint64_t *)output) = nodes.size();
    output += sizeof(uint64_t);

    //Iterate through all nodes, writing them out
    for( std::tr1::unordered_map<inode_t, ShinyMetaNode *>::iterator itty = nodes.begin(); itty != nodes.end(); ++itty ) {
        //We're adding this in out here, because it's something the filesystem needs to be aware of,
        //not something the node should handle itself
        *((uint8_t *)output) = (*itty).second->getNodeType();
        output += sizeof(uint8_t);
        
        //Now actually serialize the node
        (*itty).second->serialize( output );
        output += (*itty).second->serializedLen();
    }
    
    this->dirty = false;
    return len;
}

void ShinyMetaFilesystem::unserialize(const char *input) {
    if( !nodes.empty() ) {
        ERROR( "We're trying to unserialize when we already have %llu nodes!", nodes.size() );
        throw "Stuff was in nodes!";
    }
    
    if( *((uint16_t *)input) != ShinyMetaFilesystem::VERSION ) {
        WARN( "Warning:  Serialized filesystem is of version %d, whereas we are running version %d!", *((uint16_t *)input), ShinyMetaFilesystem::VERSION );
    }
    input += sizeof(uint16_t);
    
    LOG( "I should be able to infer this at load time" );
    this->nextInode = *((inode_t *)input);
    input += sizeof(inode_t);
    
    uint64_t numInodes = *((uint64_t *)input);
    input += sizeof(uint64_t);
    
    for( uint64_t i=0; i<numInodes; ++i ) {
        //First, read in the uint8_t of type information;
        uint8_t type = *((uint8_t *)input);
        input += sizeof(uint8_t);
        
        switch( type ) {
            case SHINY_NODE_TYPE_DIR: {
                ShinyMetaDir * newNode = new ShinyMetaDir( input, this );
                input += newNode->serializedLen();
                break; }
            case SHINY_NODE_TYPE_FILE: {
                ShinyMetaFile * newNode = new ShinyMetaFile( input, this );
                input += newNode->serializedLen();
                break; }
            case SHINY_NODE_TYPE_ROOTDIR: {
                ShinyMetaRootDir * newNode = new ShinyMetaRootDir( input, this );
                input += newNode->serializedLen();
                this->root = newNode;
                break; }
            default:
                WARN( "Stream sync error!  unknown node type %d!", type );
                break;
        }
    }
}

void ShinyMetaFilesystem::flush( bool serializeAndSave ) {
    //I _could_ have just iterated over every inode in the fs....... orrrrr, I could do this.  :P
    //Besides, this will work better for partial tree loading anyway.  :P
    if( this->isDirty() )
        this->root->flush();
    
    //If we should save it out to disk
    if( serializeAndSave ) {
        char * output;
        uint64_t len = this->serialize( &output );

        //Open it for writing, write it, and close
        int fd = open( this->fscache, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU | S_IRWXG | S_IROTH );
        if( !fd ) {
            ERROR( "Could not flush serialized output to %s!", this->fscache );
        } else {
            write( fd, output, len );
            close( fd );
        }
    }
}


inode_t ShinyMetaFilesystem::genNewInode() {
    inode_t probeNext = this->nextInode + 1;
    //Just linearly probe for the next open inode
    while( this->nodes.find(probeNext) != this->nodes.end() ) {
        if( ++probeNext == this->nextInode ) {
            ERROR( "ShinyMetaFilesystem->genNewInode(): inode space exhausted!\n" );
            throw "inode space exhausted!";
        }
    }
    inode_t toReturn = this->nextInode;
    this->nextInode = probeNext;
    return toReturn;
}

const char * ShinyMetaFilesystem::getFilecache( void ) {
    return this->filecache;
}

void ShinyMetaFilesystem::printDir( ShinyMetaDir * dir, const char * prefix ) {
    //prefix contains the current dir's name
    LOG( "[%llu] %s/\n", dir->getInode(), prefix );
    
    //Iterate over all children
    const std::list<inode_t> * listing = dir->getListing();
    for( std::list<inode_t>::const_iterator itty = listing->begin(); itty != listing->end(); ++itty ) {
        inode_t childInode = *itty;
        const char * childName = this->nodes[childInode]->getName();
        char * newPrefix = new char[strlen(prefix) + 2 + strlen(childName) + 1];
        sprintf( newPrefix, "%s/%s", prefix, childName );
        if( this->nodes[*itty]->getNodeType() == SHINY_NODE_TYPE_DIR ) {
            this->printDir( (ShinyMetaDir *)this->nodes[*itty], newPrefix );
        } else {
            //Only print it out here if it's not a directory, because directories print themselves
            LOG( "[%llu] %s\n", childInode, newPrefix );
        }
        delete( newPrefix );
    }
}

void ShinyMetaFilesystem::print( void ) {
    printDir( (ShinyMetaDir *)root, "" );
}