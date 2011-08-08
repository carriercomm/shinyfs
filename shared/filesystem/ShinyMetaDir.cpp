#include "ShinyMetaDir.h"
#include "ShinyMetaFilesystem.h"
#include "base/Logger.h"

ShinyMetaDir::ShinyMetaDir( ShinyMetaFilesystem * fs, const char * newName ) : ShinyMetaNode( fs, newName ) {
}

ShinyMetaDir::ShinyMetaDir( ShinyMetaFilesystem * fs, const char * newName, ShinyMetaDir * parent ) : ShinyMetaNode( fs, newName ) {
    parent->addNode( this );
}

ShinyMetaDir::ShinyMetaDir( const char * serializedInput, ShinyMetaFilesystem * fs ) : ShinyMetaNode( serializedInput, fs ) {
    this->unserialize( serializedInput + ShinyMetaNode::serializedLen() );
}

void ShinyMetaDir::addNode( ShinyMetaNode *newNode ) {
    LOG( "Check for cycles here!" );
    //Insert so that list is always sorted in ascending inode order
    std::list<inode_t>::iterator itty = nodes.begin();
    while( newNode->getInode() >= (*itty) ) {
        if( itty == nodes.end() )
            break;
        ++itty;
    }
    nodes.insert(itty, newNode->getInode() );
    newNode->addParent( this->inode );
}

void ShinyMetaDir::delNode(ShinyMetaNode *delNode) {
    this->nodes.remove( delNode->getInode() );
    delNode->delParent( this->inode );
}

const std::list<inode_t> * ShinyMetaDir::getListing( void ) {
    return &nodes;
}

bool ShinyMetaDir::check_childrenHaveUsAsParent( void ) {
    bool retVal = true;
    //Iterate through all nodes
    std::list<inode_t>::iterator itty = this->nodes.begin();
    while( itty != this->nodes.end() ) {
        //Find the child node
        ShinyMetaNode * node = this->fs->findNode( *itty );
        const std::list<inode_t> * parents = node->getParents();
        bool has_us_as_parent = false;
        //Iterate through all children, looking for us
        for( std::list<inode_t>::const_iterator cItty = parents->begin(); cItty != parents->end(); ++cItty ) {
            if( *cItty == this->inode ) {
                has_us_as_parent = true;
                break;
            }
        }
        
        //If it's not there, then, fix it!
        if( !has_us_as_parent ) {
            const char * path = this->getPath();
            WARN( "Child %s/%s [%llu] did not point to parent %s [%llu], when it should have!\n", path, node->getName(), node->getInode(), path, this->inode );
            node->addParent( this->inode );
            delete( path );
            retVal = false;
        }
        ++itty;
    }
    return retVal;
}

bool ShinyMetaDir::sanityCheck() {
    //First, do the basic stuff
    bool retVal = ShinyMetaNode::sanityCheck();
    
    //Next, check all children exist
    retVal &= check_existsInFs( &this->nodes, "nodes" );
    
    //Check no dups in children
    retVal &= check_noDuplicates( &this->nodes, "nodes" );
    
    //Finally, check all nodes point to us
    retVal &= check_childrenHaveUsAsParent();
    
    return retVal;
}

uint64_t ShinyMetaDir::serializedLen( void ) {
    //Return base amount + an inode for each child node, plus a counter for the number of nodes we have in this dir.
    uint64_t len = ShinyMetaNode::serializedLen();
    len += sizeof(inode_t)*(1 + this->nodes.size() );
    return len;
}

void ShinyMetaDir::serialize( char * output ) {
    //First, the normal stuffage
    ShinyMetaNode::serialize(output);
    output += ShinyMetaNode::serializedLen();
    
    //Now, save out the list.  First, the number of inodes contained
    *((uint64_t *)output) = nodes.size();
    output += sizeof(uint64_t);
    
    //Next, we pound out all the inodes contained in nodes
    inode_t * inode_output = (inode_t *)output;
    for( std::list<inode_t>::iterator itty = nodes.begin(); itty != nodes.end(); ++itty ) {
        *inode_output = *itty;
        inode_output++;
    }
}

void ShinyMetaDir::unserialize(const char *input) {
    //Next, the number of nodes to read in
    uint64_t numNodes = *((uint64_t *)input);
    input += sizeof(uint64_t);
    
    //Finally, read in all the nodes
    for( uint64_t i = 0; i<numNodes; ++i ) {
        inode_t newNode = *((inode_t *)input);
        input += sizeof(inode_t);
        
        nodes.push_back( newNode );
    }
}

ShinyNodeType ShinyMetaDir::getNodeType( void ) {
    return SHINY_NODE_TYPE_DIR;
}