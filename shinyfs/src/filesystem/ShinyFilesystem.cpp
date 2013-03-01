
#include "ShinyFilesystem.h"
#include "ShinyMetaFile.h"
#include "ShinyMetaDir.h"
#include "ShinyMetaRootDir.h"
#include <base/Logger.h>

//Used to stat() to tell if the directory exists
#include <sys/stat.h>
#include <sys/fcntl.h>


// ShinyFilesystem constructor, takes in path to cache location? I need to split this out into a separate cache object.....
ShinyFilesystem::ShinyFilesystem( const char * filecache ) : db( filecache ), root(NULL) {
    // Attempt to load the size of the metadata that was saved, if it exists, then
    // continue loading from the db. Otherwise, we need to start from scratch.
    char sizeBuff[sizeof(uint64_t)];
    if( this->db.get( this->getShinyFilesystemSizeDBKey(), sizeBuff, sizeof(uint64_t) ) == sizeof(uint64_t) ) {
        uint64_t serializedLen = *((uint64_t *)&sizeBuff[0]);
        char * serializedData = new char[serializedLen];
        if( this->db.get( this->getShinyFilesystemDBKey(), serializedData, serializedLen ) == serializedLen ) {
            this->root = dynamic_cast<ShinyMetaRootDir *>(this->unserialize( serializedData ));
        } else
            WARN( "Corrupt/missing metadata: throwing it all away!" );
    } else
        WARN( "Corrupt/missing metadata length: throwing it all away!" );
    
    // If we have no root, then "nothing remains" and we must make something entertaining up.
    if( !root ) {
        LOG( "Making crap up!" );
        this->root = new ShinyMetaRootDir( this );
        ShinyMetaFile * testf = new ShinyMetaFile( "test", this->root );
        const char * testdata = "this is a test\nawwwwww yeahhhhh\nf7u12 much?\n";
        testf->write(0, testdata, strlen(testdata) );
        
        testf = new ShinyMetaFile( "asdf", this->root );
        testdata = "blahblahblah\n";
        testf->write(0, testdata, strlen(testdata) );

    }
}

ShinyFilesystem::~ShinyFilesystem() {
    this->save();
    
    //Clear out the nodes (amazing how they just take care of themselves, so nicely and all!)
    delete( this->root );
}

//Searches a ShinyMetaDir's listing for a name, returning the child
ShinyMetaNodeSnapshot * ShinyFilesystem::findMatchingChild( ShinyMetaDirSnapshot * parent, const char * childName, uint64_t childNameLen ) {
    const std::vector<ShinyMetaNodeSnapshot *> list = *parent->getNodes();
    for( uint64_t i = 0; i < list.size(); ++i ) {
        // Compare names
        if( strlen(list[i]->getName()) == childNameLen && memcmp( list[i]->getName(), childName, childNameLen ) == 0 ) {
            // If it works, return this index we iterated over
            return list[i];
        }
    }
    //If we made it all the way through without finding a match for that file, quit out
    return NULL;
}

ShinyMetaNodeSnapshot * ShinyFilesystem::findNode( const char * path ) {
    if( path[0] != '/' ) {
        WARN( "path %s is unacceptable, must be an absolute path!", path );
        return NULL;
    }
    
    ShinyMetaNodeSnapshot * currNode = this->root;
    unsigned long filenameBegin = 1;
    for( unsigned int i=1; i<strlen(path); ++i ) {
        if( path[i] == '/' ) {
            //If this one actually _is_ a directory, let's get its listing
            if( currNode->getNodeType() == ShinyMetaNodeSnapshot::TYPE_DIR || currNode->getNodeType() == ShinyMetaNodeSnapshot::TYPE_ROOTDIR ) {
                //Search currNode's children for a name match
                ShinyMetaNodeSnapshot * childNode = findMatchingChild( dynamic_cast<ShinyMetaDir *>(currNode), &path[filenameBegin], i - filenameBegin );
                if( !childNode )
                    return NULL;
                else {
                    currNode = childNode;
                    filenameBegin = i+1;
                }
            } else
                return NULL;
            
            //Update the beginning of the next filename to be the character after this slash
            filenameBegin = i+1;
        }
    }
    if( filenameBegin < strlen(path) ) {
        ShinyMetaNodeSnapshot * childNode = findMatchingChild( dynamic_cast<ShinyMetaDir *>(currNode), &path[filenameBegin], strlen(path) - filenameBegin );
        if( !childNode )
            return NULL;
        currNode = childNode;
    }
    return currNode;
}

ShinyMetaDirSnapshot * ShinyFilesystem::findParentNode( const char *path ) {
    //Start at the end of the string
    uint64_t len = strlen( path );
    uint64_t end = len-1;
    
    //Move back another char if the last one is actually a slash
    if( path[len-1] == '/' )
        end--;
    
    //Move back until we get _another_ slash
    while( end > 1 && path[end] != '/' )
        end--;
    
    //Copy over until the end to get just the parent path
    char * newPath = new char[end + 1];
    memcpy( newPath, path, end );
    newPath[end] = 0;
    
    //Find it and return
    ShinyMetaDirSnapshot * ret = dynamic_cast<ShinyMetaDirSnapshot *>(this->findNode( newPath ));
    delete( newPath );
    return ret;
}

const char * ShinyFilesystem::getNodePath( ShinyMetaNodeSnapshot *node ) {
    // If we've cached the result from a previous call, then just return that!
    std::unordered_map<ShinyMetaNodeSnapshot *, const char *>::iterator itty = this->nodePaths.find( node );
    if( itty != this->nodePaths.end() ) {
        return (*itty).second;
    }
    
    // First, get the length:
    uint64_t len = 0;
    
    // We'll store all the paths in here so we don't have to traverse the tree multiple times
    std::list<const char *> paths;
    paths.push_front( node->getName() );
    len += strlen( node->getName() );
    
    // Now iterate up the tree, gathering the names of parents and shoving them into the list of paths
    ShinyMetaNodeSnapshot * currNode = node;
    while( currNode != (ShinyMetaNode *) this->root ) {
        // Move up in the chain of parents
        ShinyMetaDirSnapshot * nextNode = currNode->getParent();

        // If our parental chain is broken, just return ?/name
        if( !nextNode ) {
            ERROR( "Parental chain for %s is broken at %s!", paths.back(), currNode->getName() );
            paths.push_front( "?" );
            break;
        }
        
        else if( nextNode != this->root ) {
            // Push node parent's path onto the list, and add its length to len
            paths.push_front( nextNode->getName() );
            len += strlen( nextNode->getName() );
        }
        currNode = nextNode;
    }
    
    // Add 1 for each slash in front of each path
    len += paths.size();
    
    // Add another 1 for the NULL character
    char * path = new char[len+1];
    path[len] = NULL;
    
    // Write out each element of [paths] preceeded by forward slashes
    len = 0;
    for( std::list<const char *>::iterator itty = paths.begin(); itty != paths.end(); ++itty ) {
        path[len++] = '/';
        strcpy( path + len, *itty );
        len += strlen( *itty );
    }
    
    // Save node result into our cached nodePaths map and return it;
    return this->nodePaths[node] = path;
}

ShinyDBWrapper * ShinyFilesystem::getDB() {
    return &this->db;
}

const char * ShinyFilesystem::getShinyFilesystemDBKey() {
    return "?shinyfs.state";
}

const char * ShinyFilesystem::getShinyFilesystemSizeDBKey() {
    return "?shinyfs.statesize";
}

bool ShinyFilesystem::sanityCheck( void ) {
    bool retVal = true;
    //Call sanity check on all of them.
    //The nodes will return false if there is an error
    return retVal;
}


uint64_t getTotalSerializedLen( ShinyMetaNodeSnapshot * start, bool recursive ) {
    // NodeType + length of actual node
    uint64_t len = sizeof(uint8_t) + start->serializedLen();
    
    if( start->getNodeType() == ShinyMetaNodeSnapshot::TYPE_DIR || start->getNodeType() == ShinyMetaNodeSnapshot::TYPE_ROOTDIR ) {
        // number of children following this brother
        len += sizeof(uint64_t);
        
        const std::vector<ShinyMetaNode *> * startNodes = dynamic_cast<ShinyMetaDir *>(start)->getNodes();
        for( uint64_t i=0; i<startNodes->size(); ++i ) {
            //Don't have to check for TYPE_ROOTDIR here, because that's an impossibility!  yay!
            if( recursive && (*startNodes)[i]->getNodeType() == ShinyMetaNodeSnapshot::TYPE_DIR )
                len += getTotalSerializedLen( (*startNodes)[i], recursive );
            else {
                // NodeType + length of actual node, just like we did for start
                len += sizeof(uint8_t) + (*startNodes)[i]->serializedLen();
            }
        }
    }
    return len;
}


// Very similar in form to the above. Thus the liberal copypasta.
// returns output, shifted by total serialized length, so just use [output - totalLen]
char * serializeTree( ShinyMetaNodeSnapshot * start, bool recursive, char * output ) {
    // write out start first
    *((uint8_t *)output) = start->getNodeType();
    output += sizeof(uint8_t);

    start->serialize(output);
    output += start->serializedLen();
    
    // Next, check if we're a dir, and if so, do the serialization dance!
    if( start->getNodeType() == ShinyMetaNodeSnapshot::TYPE_DIR || start->getNodeType() == ShinyMetaNodeSnapshot::TYPE_ROOTDIR ) {
        // Write out the number of children
        *((uint64_t *)output) = dynamic_cast<ShinyMetaDirSnapshot *>(start)->getNumNodes();
        output += sizeof(uint64_t);
        
        //Write out all children
        const std::vector<ShinyMetaNodeSnapshot *> * startNodes = dynamic_cast<ShinyMetaDirSnapshot *>(start)->getNodes();
        for( uint64_t i=0; i<startNodes->size(); ++i ) {
            //Don't have to check for TYPE_ROOTDIR here, because that's an impossibility!  yay!
            if( recursive && (*startNodes)[i]->getNodeType() == ShinyMetaNodeSnapshot::TYPE_DIR ) {
                output = serializeTree( (*startNodes)[i], recursive, output );
            } else {            
                *((uint8_t *)output) = (*startNodes)[i]->getNodeType();
                output += sizeof(uint8_t);

                output = (*startNodes)[i]->serialize(output);
            }
        }
    }
    return output;
}

uint64_t ShinyFilesystem::serialize( char ** store, ShinyMetaNodeSnapshot * start, bool recursive ) {
    // First, we're going to find the total length of this serialized monstrosity
    // We'll start with the itty bitty overhead of the version number
    uint64_t len = sizeof(uint16_t);

    // default to root (darn you C++, not allowing me to set a default value of this->root!)
    if( !start )
        start = this->root;
    
    // To find total length, walk the walk and talk the talk
    len = getTotalSerializedLen( start, recursive );
    
    // Now, reserve buffer space.  This is such an anti-climactic line, it makes me kind of sad.
    char * output = new char[len];
    
    // make sure that when we shove stuff into output, the user gets it in their variable
    // I create the "output" variable just so that I'm not doing 1253266246x pointer dereferences,
    // also so that the output is unaffected by the crazy shifting that goes on during the serialization
    *store = output;
    
    // First, write out the version number
    *((uint16_t *)output) = this->getVersion();
    output += sizeof(uint16_t);
    
    // Next, we'll iterate through all the nodes we're going to serialize, doing them one at a time;
    serializeTree( start, recursive, output );
    
    // and finally, return the length!
    return len;
}

ShinyMetaNode * ShinyFilesystem::unserializeTree( const char ** input, ShinyMetaDir * parent ) {
    uint8_t type = *((uint8_t *)*input);
    *input += sizeof(uint8_t);
    
    switch( type ) {
        case ShinyMetaNodeSnapshot::TYPE_ROOTDIR:
        case ShinyMetaNodeSnapshot::TYPE_DIR:
        {
            // First, get the (root)dir itself. In other news, WHY THE HECK DO I WRITE THINGS LIKE THIS?!
            ShinyMetaDir * newDir = (type == ShinyMetaNodeSnapshot::TYPE_ROOTDIR) ? new ShinyMetaRootDir( input, this ) : new ShinyMetaDir( input, parent );
            
            // next, get the number of children that have been serialized
            uint64_t numNodes = *((uint64_t *)*input);
            *input += sizeof(uint64_t);
            
            // Now, iterating over all children of this dir, LOAD 'EM IN!
            for( uint64_t i=0; i<numNodes; ++i ) {
                // The cycle continues...... we pass our troubles onto our own children.
                // Note that we don't actually use the return value of unserializeTree, as the child will automagically
                // get added to newDir by its constructor
                unserializeTree( input, newDir );
            }
            return newDir;
        }
        case ShinyMetaNodeSnapshot::TYPE_FILE:
            return new ShinyMetaFile( input, parent );
        default:
            WARN( "Unknown node type (%d)!", type );
            break;
    }
    // When in doubt, return NULL!
    return NULL;
}

ShinyMetaRootDir * ShinyFilesystem::unserialize( const char *input ) {
    // First, a version check
    if( *((uint16_t *)input) != this->getVersion() ) {
        ERROR( "Serialized filesystem objects are of version %d, whereas we are compatible with version %d!", *((uint16_t *)input), this->getVersion() );
        return NULL;
    }
    // now gracefully scoot past that short
    input += sizeof(uint16_t);
    
    // If a problem is too hard for you, push it off to another function! Preferablly, a recursive helper function!
    ShinyMetaNode * possibleRoot = unserializeTree( &input );
    
    // Check to make sure we at least have a root node
    if( possibleRoot->getNodeType() != ShinyMetaNodeSnapshot::TYPE_ROOTDIR ) {
        ERROR( "Corrupt root node type" );
        return NULL;
    }
    
    // If we do, return it!
    return (ShinyMetaRootDir *)possibleRoot;
}

void ShinyFilesystem::save() {
    // First, serialize everything out
    char * output;
    uint64_t len = this->serialize( &output );
    
    // Send this to the filecache to write out
    this->db.put( this->getShinyFilesystemDBKey(), output, len );
    this->db.put( this->getShinyFilesystemSizeDBKey(), (const char *)&len, sizeof(uint64_t) );
    
    // TESTING TESTING TESTING
    ShinyMetaRootDir * testRoot = this->unserialize( output );
    if( testRoot ) {
        this->printDir( testRoot );
        LOG("---------------------------------------------------------------------");
        this->printDir( this->root );
    }
    
    delete( output );
}


void ShinyFilesystem::printDir( ShinyMetaDir * dir, const char * prefix ) {
    //prefix contains the current dir's name
    LOG( "%s/\n", prefix );
    
    //Iterate over all children
    const std::vector<ShinyMetaNode *> nodes = *dir->getNodes();
    for( uint64_t i=0; i<nodes.size(); ++i ) {
        const char * childName = nodes[i]->getName();
        char * newPrefix = new char[strlen(prefix) + 2 + strlen(childName) + 1];
        sprintf( newPrefix, "%s/%s", prefix, childName );
        if( nodes[i]->getNodeType() == ShinyMetaNodeSnapshot::TYPE_DIR ) {
            this->printDir( (ShinyMetaDir *)nodes[i], newPrefix );
        } else {
            //Only print it out here if it's not a directory, because directories print themselves
            if( nodes[i]->getNodeType() == ShinyMetaNodeSnapshot::TYPE_FILE )
                LOG( "%s (%d)", newPrefix, ((ShinyMetaFile *)nodes[i])->getLen() );
            else
                LOG( "%s", newPrefix );
        }
        delete( newPrefix );
    }
}

void ShinyFilesystem::print( void ) {
    printDir( (ShinyMetaDir *)root, "" );
}

const uint64_t ShinyFilesystem::getVersion() {
    return ShinyFilesystem::VERSION;
}
/**/