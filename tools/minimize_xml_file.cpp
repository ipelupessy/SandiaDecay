/* SandiaDecay: a library that provides nuclear decay info and calculations.
 
 Copyright 2018 National Technology & Engineering Solutions of Sandia, LLC
 (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
 Government retains certain rights in this software.
 For questions contact William Johnson via email at wcjohns@sandia.gov, or
 alternative email of interspec@sandia.gov.
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iterator>

#include "SandiaDecay.h"

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_utils.hpp"
#include "rapidxml/rapidxml_print.hpp"


using namespace std;

/** Program to remove the edit history of sandia.decay.xml. */

//forward declarations
void remove_id_attrib( rapidxml::xml_node<char> *node );
void check_revision_attrib( rapidxml::xml_attribute<char> *att );
void shrink_xml_elements( rapidxml::xml_node<char> *doc_node );
void test_db_still_same( const string orig_xml_filename,
                         const string final_xml_filename,
                         const bool coincidences_removed );
template <typename pod>
std::string pod_to_str( const pod &val );


//Usuage:
//XML_FOREACH_DAUGHTER( child_node_variable, parent_node, "ChildElementName" ){
//  assert( child_node_variable->name() == string("ChildElementName") );
// }
#define XML_FOREACH_DAUGHTER( nodename, parentnode, daughternamestr ) \
for( rapidxml::xml_node<char> *nodename = parentnode->first_node(daughternamestr); \
  nodename; \
  nodename = nodename->next_sibling(daughternamestr) )



int main( int argc, char **argv )
{
  vector<string> args;
  
  /* Removing ID attributes shrinks file from 54MB to 32MB.
     Removing concidences ad ID attribs shrinks to 9.6MB
     Further shrinking tag names gets you down to 6MB
   */
  bool do_remove_id_attrib = false; //setting to true reduces output size from 54MB to 32MB
  bool do_remove_coincidence_gamma = false; //setting to true reduces file size from 32MB to 9.6M
  bool do_shrink_coincidence = false; //reduces ID attribute value to minimum size needed, removes unecassary info from XML.
  bool do_shrink_tag_names = false; //Shrinks XML element and attribute names to one or two character names, ex "nuclide"->"n", "intensity"->"i", etc.  Also removes trailing zeroes from float values.
  
  for( int i = 1; i < argc; ++i )
  {
    const string arg = argv[i];
    if( arg == "--remove-id-attrib" )
      do_remove_id_attrib = true;
    else if( arg == "--remove-coinc" )
      do_remove_coincidence_gamma = true;
    else if( arg == "--shrink-coinc" )
      do_shrink_coincidence = true;
    else if( arg == "--shrink-tag-names" )
      do_shrink_tag_names = true;
    else
      args.push_back( arg );
  }//for( loop over the
  
  
  if( args.size() != 2 )
  {
    fprintf( stderr, "Usage: %s <optional flags> <input sandia.decay.xml> <output file>\n"
            "\tWhere optional flags are '--remove-id-attrib', '--remove-coinc', '--shrink-coinc' and/or '--shrink-tag-names'.\n"
            "\t e.x.: ./%s --remove-id-attrib input.sandia.decay.xml output.sandia.decay.xml\n",
            argv[0], argv[0] );
    return EXIT_FAILURE;
  }//if( argc != 3 )
  
  if( do_remove_id_attrib && !do_remove_coincidence_gamma )
  {
    fprintf( stderr, "You cannot remove ID attribute without removing conicidence partivles.\n" );
    return EXIT_FAILURE;
  }
  
  try
  {
    rapidxml::file<char> xmlfile( args[0].c_str() );
    
    rapidxml::xml_document<char> doc;
    doc.parse<rapidxml::parse_full>( xmlfile.data() );
    rapidxml::xml_node<char> *doc_node = doc.first_node();
  
    if( doc_node->first_attribute("version") )
      doc_node = doc_node->next_sibling();
    
    int ntrans = 0, nodes_removed = 0;
    
    vector<rapidxml::xml_node<char> *> nuc_to_delete;
    for( rapidxml::xml_node<char> *nuc = doc_node->first_node( "nuclide" );
        nuc;
        nuc = nuc->next_sibling("nuclide") )
    {
      rapidxml::xml_attribute<char> *revision = nuc->first_attribute( "revision" );
      check_revision_attrib( revision );
      
      if( revision && rapidxml::internal::compare(revision->value(), revision->value_size(), "deleted", 7, false ) )
      {
        nuc_to_delete.push_back( nuc );
        continue;
      }
      
      if( revision )
        nuc->remove_attribute( revision );
      
      vector<rapidxml::xml_node<char> *> to_delete;
      for( rapidxml::xml_node<char> *child = nuc->first_node(); child; child = child->next_sibling() )
      {
        to_delete.push_back( child );
      }
      for( size_t i = 0; i < to_delete.size(); ++i )
        nuc->remove_node( to_delete[i] );
    }//for( loop over nuclides )
    
    for( size_t i = 0; i < nuc_to_delete.size(); ++i )
      doc_node->remove_node( nuc_to_delete[i] );
    
    for( rapidxml::xml_node<char> *xmltrans = doc_node->first_node( "transition" );
        xmltrans;
        xmltrans = xmltrans->next_sibling("transition") )
    {
      rapidxml::xml_attribute<char> *child_label_attrib = xmltrans->first_attribute("child");
      rapidxml::xml_attribute<char> *parent_label_attrib = xmltrans->first_attribute("parent");
      
      const string parent_nuc = child_label_attrib ? child_label_attrib->value() : "";
      const string child_nuc = parent_label_attrib ? parent_label_attrib->value() : "";
      
      vector<bool> gamma_uuid_used;
      vector<string> gamma_uuid;
      vector<rapidxml::xml_node<char> *> gamma_uuid_nodes;
      for( rapidxml::xml_node<char> *child = xmltrans->first_node("gamma");
          child;
          child = child->next_sibling("gamma") )
      {
        rapidxml::xml_attribute<char> *attrib = child->first_attribute("id");
        if( attrib && attrib->value_size() )
        {
          gamma_uuid.push_back( attrib->value() );
          gamma_uuid_used.push_back( false );
          gamma_uuid_nodes.push_back( child );
        }
      }//
      
      
      vector<rapidxml::xml_node<char> *> to_delete;
      for( rapidxml::xml_node<char> *child = xmltrans->first_node();
          child;
          child = child->next_sibling() )
      {
        if( do_remove_id_attrib )
          remove_id_attrib( child );
        
        rapidxml::xml_attribute<char> *id_sttriv = child->first_attribute( "id" );
        rapidxml::xml_attribute<char> *revision = child->first_attribute( "revision" );
        
        check_revision_attrib( revision );
        
        const string child_uuid = id_sttriv ? id_sttriv->value() : "";
        
        if( rapidxml::internal::compare(child->name(), child->name_size(), "insertion", 9, false )
           || rapidxml::internal::compare(child->name(), child->name_size(), "edit", 4, false ) )
        {
          to_delete.push_back( child );
        }else if( revision && rapidxml::internal::compare(revision->value(), revision->value_size(), "deleted", 7, false ) )
        {
          to_delete.push_back( child );
        }else if( child->type() == rapidxml::node_comment )
        {
          to_delete.push_back( child );
        }else
        {
          if( rapidxml::internal::compare(child->name(),child->name_size(),"beta",4,false)
             || rapidxml::internal::compare(child->name(),child->name_size(),"positron",8,false)
             || rapidxml::internal::compare(child->name(),child->name_size(),"electronCapture",15,false)
             || rapidxml::internal::compare(child->name(),child->name_size(),"alpha",5,false) )
          {
            rapidxml::xml_attribute<char> *attrib = child->first_attribute("id");
            if( attrib )
              child->remove_attribute( attrib );
          }
          
          vector<rapidxml::xml_node<char> *> gchild_to_delete;
          for( rapidxml::xml_node<char> *gchild = child->first_node();
              gchild;
              gchild = gchild->next_sibling() )
          {
            if( do_remove_id_attrib )
              remove_id_attrib( gchild );
            
            if( rapidxml::internal::compare(gchild->name(), gchild->name_size(), "edit", 4, false )
               || rapidxml::internal::compare(gchild->name(), gchild->name_size(), "insertion", 9, false )
               || rapidxml::internal::compare(gchild->name(), gchild->name_size(), "deletion", 8, false )
               || (do_remove_coincidence_gamma && rapidxml::internal::compare(gchild->name(),gchild->name_size(),"coincidentGamma",15,false) )
               || gchild->type() == rapidxml::node_comment )
            {
              gchild_to_delete.push_back( gchild );
            }else if( gchild->name_size() < 1 )
            {
              printf( "Found un-named node\n" );
            }else if( rapidxml::internal::compare(gchild->name(), gchild->name_size(), "coincidentgamma", 15, false ) )
            {
              if( do_shrink_coincidence )
              {
                //gchild->name("coinc");
                rapidxml::xml_attribute<char> *attrib = gchild->first_attribute("energy");
                if( attrib )
                  gchild->remove_attribute( attrib );
                
                attrib = gchild->first_attribute("id");
                if( attrib )
                {
                  //child_uuid
                  const string gchild_uuid = attrib->value();
                  assert( gchild_uuid != child_uuid );
                  vector<string>::const_iterator pos = std::find( gamma_uuid.begin(), gamma_uuid.end(), gchild_uuid );
                  //assert( pos != gamma_uuid.end() );
                  if( pos == gamma_uuid.end() )
                  {
                    fprintf( stderr, "Warning: Could not find UUID='%s' for %s->%s\n",
                            gchild_uuid.c_str(), parent_nuc.c_str(), child_nuc.c_str() );
                  }else
                  {
                    const int64_t index = pos - gamma_uuid.begin();
                    gamma_uuid_used[index] = true;
                    const string newuuid = pod_to_str(index);
                    const char *newuuidstr = doc.allocate_string( newuuid.c_str() );
                    attrib->value( newuuidstr );
                  }
                }
              }//if( do_shrink_coincidence )
              
            }else //if( !rapidxml::internal::compare(gchild->name(), gchild->name_size(), "coincidentgamma", 15, false ) )
            {
              throw runtime_error( "Unknown gchild node: '"
                                   + std::string(gchild->name(), gchild->name() + gchild->name_size()) + "'" );
            }
          }
          for( size_t i = 0; i < gchild_to_delete.size(); ++i )
            child->remove_node( gchild_to_delete[i] );
        
          if( revision )
            child->remove_attribute( revision );
        }
      }//for( loop over children )
    
      rapidxml::xml_attribute<char> *attrib = xmltrans->first_attribute("remarks");
      if( attrib )
      {
        string val = attrib->value();
        if( (val.find("warning:") == string::npos) && (val.find("audit:") == string::npos) )
          xmltrans->remove_attribute( attrib );
      }
      
      
      if( do_shrink_coincidence )
      {
        for( size_t i = 0; i < gamma_uuid.size(); ++i )
        {
          rapidxml::xml_attribute<char> *attrib = gamma_uuid_nodes[i]->first_attribute("id");
          
          if( !gamma_uuid_used[i] )
          {
            if( attrib )
              gamma_uuid_nodes[i]->remove_attribute(attrib);
          }else
          {
            assert( attrib );
            const char *newuuidstr = doc.allocate_string( pod_to_str(i).c_str() );
            attrib->value( newuuidstr );
          }
        }
      }//if( do_shrink_coincidence )
      
      
      nodes_removed += to_delete.size();
      for( size_t i = 0; i < to_delete.size(); ++i )
        xmltrans->remove_node( to_delete[i] );
      
      ++ntrans;
    }//loop over transitions.
    
  
    for( rapidxml::xml_node<char> *node = doc_node->first_node(); node;  )
    {
      rapidxml::xml_node<char> *next_node = node->next_sibling();
      if( node->type() == rapidxml::node_comment )
      {
        doc_node->remove_node( node );
        ++nodes_removed;
      }
      
      node = next_node;
    }
    
    if( do_shrink_tag_names )
      shrink_xml_elements( doc_node );
      
    printf( "There were %i transitions; removed %i xml elements.\n", ntrans, nodes_removed );
    
    {
      ifstream test( args[1].c_str() );
      if( test.is_open() )
        throw runtime_error( "File '" + args[1] + " already exists - not overwriting!" );
    }
    
    {
      ofstream outputfile( args[1].c_str(), ios::out | ios::binary );
      if( !outputfile )
        throw runtime_error( "Could not open output file: " + string(args[1]) );
    
      rapidxml::print<char>( outputfile, doc, 0 );
    }
    
    printf( "Saved output file '%s'\n", args[1].c_str() );
    
    test_db_still_same( args[0], args[1], do_remove_coincidence_gamma );
  }catch( std::exception &e )
  {
    fprintf( stderr, "Error: %s\n", e.what() );
  }//try / catch
  
  
  
  return EXIT_SUCCESS;
}//int main( int argc, char **argv )



void test_db_still_same( const string orig_xml_filename, const string final_xml_filename, const bool coincidences_removed )
{
  using namespace SandiaDecay;
  
  SandiaDecayDataBase olddb( orig_xml_filename );
  SandiaDecayDataBase newdb( final_xml_filename );
  
  const vector<const Nuclide *> &oldnucs = olddb.nuclides();
  const vector<const Nuclide *> &newnucs = newdb.nuclides();
  
  if( oldnucs.size() != newnucs.size() )
    throw runtime_error( "Number of nuclides changed" );
  
  for( size_t i = 0; i < oldnucs.size(); ++i )
  {
    const Nuclide * const oldnuc = oldnucs[i];
    const Nuclide * const newnuc = newnucs[i];
    
    if( oldnuc->symbol != newnuc->symbol )
      throw runtime_error( "old and new symbols dont match" );
    
    if( oldnuc->atomicNumber != newnuc->atomicNumber )
      throw runtime_error( "old and new  dont match" );
    
    if( oldnuc->massNumber != newnuc->massNumber )
      throw runtime_error( "old and new  dont match" );
    
    if( oldnuc->isomerNumber != newnuc->isomerNumber )
      throw runtime_error( "old and new  dont match" );
    
    if( oldnuc->atomicMass != newnuc->atomicMass )
      throw runtime_error( "old and new  dont match" );
    
    if( oldnuc->halfLife != newnuc->halfLife )
      throw runtime_error( "old and new  dont match" );
    
    if( oldnuc->decaysFromParents.size() != newnuc->decaysFromParents.size() )
      throw runtime_error( "old and new number of transitions from parents dont match for " + oldnuc->symbol
                          + "(" + pod_to_str(oldnuc->decaysFromParents.size()) + " vs "
                           + pod_to_str(newnuc->decaysFromParents.size()) + ")" );
    
    if( oldnuc->decaysToChildren.size() != newnuc->decaysToChildren.size() )
      throw runtime_error( "old and new number of transitions to children dont match for " + oldnuc->symbol
                          + "(" + pod_to_str(oldnuc->decaysFromParents.size()) + " vs "
                          + pod_to_str(newnuc->decaysFromParents.size()) + ")" );
  
    std::vector<const Transition *> oldtrans = oldnuc->decaysFromParents;
    oldtrans.insert( oldtrans.end(), oldnuc->decaysToChildren.begin(), oldnuc->decaysToChildren.end() );
    
    std::vector<const Transition *> newtrans = newnuc->decaysFromParents;
    newtrans.insert( newtrans.end(), newnuc->decaysToChildren.begin(), newnuc->decaysToChildren.end() );
    
    assert( oldtrans.size() == newtrans.size() );
    
    for( size_t trans_index = 0; trans_index < oldtrans.size(); ++trans_index )
    {
      const Transition * const oldtran = oldtrans[trans_index];
      const Transition * const newtran = newtrans[trans_index];
      
      if( (!oldtran->parent) != (!newtran->parent) )
        throw runtime_error( "Availability of parent on old/new transition not equal" );
      
      if( (!oldtran->child) != (!newtran->child) )
        throw runtime_error( "Availability of parent on old/new transition not equal" );
      
      if( oldtran->parent && (oldtran->parent->symbol != newtran->parent->symbol) )
        throw runtime_error( "Transition parent not equal" );
      
      if( oldtran->child && (oldtran->child->symbol != newtran->child->symbol) )
        throw runtime_error( "Transition child not equal" );
      
      if( oldtran->mode != newtran->mode )
        throw runtime_error( "Transition decay mode not equal" );
      
      if( oldtran->branchRatio != newtran->branchRatio )
        throw runtime_error( "Transition branchRatio not equal" );
      
      if( oldtran->products.size() != newtran->products.size() )
        throw runtime_error( "Transition number of products not the same size" );
      
      for( size_t prod_index = 0; prod_index < oldtran->products.size(); ++prod_index )
      {
        const RadParticle &oldpart = oldtran->products[prod_index];
        const RadParticle &newpart = newtran->products[prod_index];
        
        if( oldpart.type != newpart.type )
          throw runtime_error( "Particle type does not match" );
        
        if( oldpart.energy != newpart.energy )
          throw runtime_error( "Particle energy does not match" );
        
        if( oldpart.intensity != newpart.intensity )
          throw runtime_error( "Particle intensity does not match" );
        
        if( oldpart.hindrance != newpart.hindrance )
          throw runtime_error( "Particle hindrance does not match" );
        
        if( oldpart.logFT != newpart.logFT )
          throw runtime_error( "Particle logFT does not match" );
        
        if( oldpart.forbiddenness != newpart.forbiddenness )
          throw runtime_error( "Particle forbiddenness does not match" );
        
        if( !coincidences_removed )
        {
          const vector< pair<unsigned short int,float> > &oldcoinc = oldpart.coincidences;
          const vector< pair<unsigned short int,float> > &newcoinc = newpart.coincidences;
          
          if( oldcoinc.size() != newcoinc.size() )
            throw runtime_error( "Number of coincidences does not match" );
          
          for( size_t coinc_index = 0; coinc_index < oldcoinc.size(); ++coinc_index )
          {
            if( oldcoinc[coinc_index].first != newcoinc[coinc_index].first )
              throw runtime_error( "Coincidence index does not match" );
            
            if( oldcoinc[coinc_index].second != newcoinc[coinc_index].second )
              throw runtime_error( "Coincidence rate does not match" );
          }//for( loop over coincidences )
        }//if( coincidenses shouldnt hav echanged )
      }//for( loop over decay particles )
    }//for( loop over transisitons )
  }//for( loop over nuclides )
  
  
  const vector<const Element *> &oldels = olddb.elements();
  const vector<const Element *> &newels = newdb.elements();
  if( oldels.size() != newels.size() )
    throw runtime_error( "Number of elements does not match" );
  
  for( size_t el_index = 0; el_index < oldels.size(); ++el_index )
  {
    const Element *oldel = oldels[el_index];
    const Element *newel = newels[el_index];
    
    if( oldel->symbol != newel->symbol )
      throw runtime_error( "Element symbols do not match" );
    
    if( oldel->name != newel->name )
      throw runtime_error( "Element name do not match" );
    
    if( oldel->atomicNumber != newel->atomicNumber )
      throw runtime_error( "Element atomicNumber do not match" );
    
    if( oldel->isotopes.size() != newel->isotopes.size() )
      throw runtime_error( "Element isotopes size is different" );
    
    if( oldel->xrays.size() != newel->xrays.size() )
      throw runtime_error( "Element xrays size is different" );
    
    for( size_t iso_index = 0; iso_index < oldel->isotopes.size(); ++iso_index )
    {
      const NuclideAbundancePair &oldiso = oldel->isotopes[iso_index];
      const NuclideAbundancePair &newiso = newel->isotopes[iso_index];
      
      if( oldiso.nuclide->symbol != newiso.nuclide->symbol )
        throw runtime_error( "Element isos nuclide do not match" );
      
      if( oldiso.abundance != newiso.abundance )
        throw runtime_error( "Element isos abundance do not match" );
    }//for( loop over isotopes )
    
    for( size_t x_index = 0; x_index < oldel->xrays.size(); ++x_index )
    {
      const EnergyIntensityPair &oldxray = oldel->xrays[x_index];
      const EnergyIntensityPair &newxray = newel->xrays[x_index];
      
      if( oldxray.energy != newxray.energy )
        throw runtime_error( "Element xray energy do not match" );
      
      if( oldxray.intensity != newxray.intensity )
        throw runtime_error( "Element intensity energy do not match" );
    }//for( loop over xrays )
  }//for( loop over elements )
  
  printf( "Parsing of old XML file and new XML file result in identical SandiaDecayDataBase objects\n" );
}//void test_db_still_same()


template <typename pod>
std::string pod_to_str( const pod &val ){
  std::stringstream srm;
  srm << val;
  return srm.str();
}


void rename_attrib( rapidxml::xml_node<char> *node, const char *oldname, const char *newname )
{
  rapidxml::xml_attribute<char> *att = node->first_attribute(oldname);
  if( att )
    att->name( newname );
}

void remove_trailing_zeroes( rapidxml::xml_attribute<char> *n )
{
  //"1.0000E-03" -> "1E-03"
  //"2.7500E-04" -> "2.75E-04"
  //"1.1000" -> "1.1"
  //"1.0000" -> "1"
  //"1.00E+00" -> "1"
  if( !n )
    return;
  
  const string origval = n->value();
  string smallval = origval;
  
  const size_t ppos = smallval.find( "." );
  const size_t epos = smallval.find_first_of( "Ee" );
  
  if( ppos == string::npos && epos == string::npos )
    return;
  
  string epart;
  if( epos != string::npos )
  {
    assert( epos != 0 );
    assert( (ppos == string::npos) || (ppos < epos) );
    epart = smallval.substr(epos);
    smallval = smallval.substr(0,epos);
    if( epart == "E+00" || epart == "e+00" || epart == "E00" )
      epart = "";
  }//if( epos != string::npos )

  if( ppos != string::npos )
  {
    while( smallval[smallval.size()-1] == '0' )
    {
      smallval = smallval.substr(0,smallval.size()-1);
      assert( smallval.size() );
    }
    
    if( smallval[smallval.size()-1] == '.' )
      smallval = smallval.substr(0,smallval.size()-1);
    if( smallval.empty() ) //original number was like "0.0000"
      smallval = "0";
  }//if( ppos != string::npos )
  
  smallval += epart;
  
  const double orig = strtod( origval.c_str(), NULL );
  const double modded = strtod( smallval.c_str(), NULL );
  if( orig != modded )
  {
    fprintf( stderr, "Removing trailing zeros of '%s' let to %f->%f\n", n->value(), orig, modded );
  }
  assert( orig == modded );
  
  n->value( n->document()->allocate_string(smallval.c_str()) );
}//void remove_trailing_zeroes(...)


void shrink_xml_elements( rapidxml::xml_node<char> *doc_node )
{
  XML_FOREACH_DAUGHTER( nuc, doc_node, "nuclide" )
  {
    nuc->name( "n" );
    
    remove_trailing_zeroes( nuc->first_attribute("atomicMass") );
    remove_trailing_zeroes( nuc->first_attribute("halfLife") );
    
    rename_attrib( nuc, "atomicMass", "am" );
    rename_attrib( nuc, "atomicNumber", "an" );
    rename_attrib( nuc, "halfLife", "hl" );
    rename_attrib( nuc, "isomerNumber", "iso" );
    rename_attrib( nuc, "massNumber", "mn" );
    rename_attrib( nuc, "symbol", "s" );
  }//foreach( nuclide )
  
  
  XML_FOREACH_DAUGHTER( trans, doc_node, "transition" )
  {
    trans->name( "t" );
    
    remove_trailing_zeroes( trans->first_attribute("branchRatio") );
    
    rename_attrib( trans, "branchRatio", "br" );
    rename_attrib( trans, "child", "c" );
    rename_attrib( trans, "mode", "m" );
    rename_attrib( trans, "parent", "p" );
    
    XML_FOREACH_DAUGHTER( child, trans, "beta" )
    {
      child->name( "b" );
      
      remove_trailing_zeroes( child->first_attribute("energy") );
      remove_trailing_zeroes( child->first_attribute("intensity") );
      remove_trailing_zeroes( child->first_attribute("logFT") );
      remove_trailing_zeroes( child->first_attribute("forbiddenness") );
      
      rename_attrib( child, "energy", "e" );
      rename_attrib( child, "intensity", "i" );
      rename_attrib( child, "logFT", "lft" );
      rename_attrib( child, "forbiddenness", "f" );
    }
    
    XML_FOREACH_DAUGHTER( child, trans, "positron" )
    {
      child->name( "p" );
      
      remove_trailing_zeroes( child->first_attribute("energy") );
      remove_trailing_zeroes( child->first_attribute("intensity") );
      remove_trailing_zeroes( child->first_attribute("logFT") );
      remove_trailing_zeroes( child->first_attribute("forbiddenness") );
      
      rename_attrib( child, "energy", "e" );
      rename_attrib( child, "intensity", "i" );
      rename_attrib( child, "logFT", "lft" );
      rename_attrib( child, "forbiddenness", "f" );
    }
    
    XML_FOREACH_DAUGHTER( child, trans, "electronCapture" )
    {
      child->name( "ec" );
      
      remove_trailing_zeroes( child->first_attribute("energy") );
      remove_trailing_zeroes( child->first_attribute("intensity") );
      remove_trailing_zeroes( child->first_attribute("logFT") );
      remove_trailing_zeroes( child->first_attribute("forbiddenness") );
      
      rename_attrib( child, "energy", "e" );
      rename_attrib( child, "intensity", "i" );
      rename_attrib( child, "logFT", "lft" );
      rename_attrib( child, "forbiddenness", "f" );
    }
    
    XML_FOREACH_DAUGHTER( child, trans, "alpha" )
    {
      child->name( "a" );
      
      remove_trailing_zeroes( child->first_attribute("energy") );
      remove_trailing_zeroes( child->first_attribute("intensity") );
      remove_trailing_zeroes( child->first_attribute("hindrance") );
      remove_trailing_zeroes( child->first_attribute("forbiddenness") );
      
      rename_attrib( child, "energy", "e" );
      rename_attrib( child, "intensity", "i" );
      rename_attrib( child, "hindrance", "h" );
      rename_attrib( child, "forbiddenness", "f" );
    }
    
    XML_FOREACH_DAUGHTER( child, trans, "xray" )
    {
      child->name( "x" );
      
      remove_trailing_zeroes( child->first_attribute("energy") );
      remove_trailing_zeroes( child->first_attribute("intensity") );
      
      rename_attrib( child, "energy", "e" );
      rename_attrib( child, "intensity", "i" );
      rename_attrib( child, "assignment", "l" );
      
      rename_attrib( child, "l1intensity", "l1i" );
      rename_attrib( child, "l2intensity", "l2i" );
      rename_attrib( child, "l3intensity", "l3i" );
      
    }
    
    XML_FOREACH_DAUGHTER( gamma, trans, "gamma" )
    {
      gamma->name( "g" );
      
      remove_trailing_zeroes( gamma->first_attribute("energy") );
      remove_trailing_zeroes( gamma->first_attribute("intensity") );
      
      rename_attrib( gamma, "energy", "e" );
      rename_attrib( gamma, "intensity", "i" );
      
      XML_FOREACH_DAUGHTER( coinc, gamma, "coincidentGamma" )
      {
        coinc->name("c");
        
        remove_trailing_zeroes( coinc->first_attribute("energy") );
        remove_trailing_zeroes( coinc->first_attribute("intensity") );
        
        rename_attrib( coinc, "energy", "e" );
        rename_attrib( coinc, "intensity", "i" );
      }
    }
  }//foreach( transition )
  
  
  XML_FOREACH_DAUGHTER( el, doc_node, "element" )
  {
    el->name( "el" );
    
    rename_attrib( el, "atomicNumber", "an" );
    rename_attrib( el, "name", "n" );
    rename_attrib( el, "symbol", "s" );
    
    XML_FOREACH_DAUGHTER( x, el, "xray" )
    {
      x->name( "x" );
      remove_trailing_zeroes( x->first_attribute("energy") );
      remove_trailing_zeroes( x->first_attribute("intensity") );
    
      rename_attrib( x, "relintensity", "i" );
      rename_attrib( x, "energy", "e" );
    }
    
    XML_FOREACH_DAUGHTER( x, el, "isotope" )
    {
      x->name( "iso" );
      rename_attrib( x, "abundance", "a" );
      rename_attrib( x, "symbol", "s" );
    }
  }//foreach( element )
}//void shrink_xml_elements( )


void remove_id_attrib( rapidxml::xml_node<char> *node )
{
  rapidxml::xml_attribute<char> *attrib = node->first_attribute("id");
  if( attrib )
    node->remove_attribute( attrib );
}


void check_revision_attrib( rapidxml::xml_attribute<char> *att )
{
  if( !att )
    return;
  const string val = att->value();
  if( val != "deleted" && val != "edited" && val != "inserted" )
    throw runtime_error( "Invalid revision attribute value: '" + val + "'" );
}





