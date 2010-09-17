<?php

date_default_timezone_set('Europe/Moscow');

$cl = new SphinxClient ();

$q = "test";
$sql = "";
$mode = SPH_MATCH_ALL;
$host = "localhost";
$port = 9312;
$index = "test1";
$groupby = "";
$groupsort = "@group desc";
$filter = "group_id";
$filtervals = array();
$distinct = "";
$sortby = "";
$limit = 20;
$ranker = SPH_RANK_PROXIMITY_BM25;

////////////
// do query
////////////

$cl->SetServer ( $host, $port );
$cl->SetConnectTimeout ( 1 );
$cl->SetArrayResult ( true );
$cl->SetFieldWeights ( array ( 100, 1 ) );
$cl->SetMatchMode ( $mode );
if ( count($filtervals) )	$cl->SetFilter ( $filter, $filtervals );
if ( $groupby )				$cl->SetGroupBy ( $groupby, SPH_GROUPBY_ATTR, $groupsort );
if ( $sortby )				$cl->SetSortMode ( SPH_SORT_EXTENDED, $sortby );
if ( $sortexpr )			$cl->SetSortMode ( SPH_SORT_EXPR, $sortexpr );
if ( $distinct )			$cl->SetGroupDistinct ( $distinct );
if ( $limit )				$cl->SetLimits ( 0, $limit, ( $limit>1000 ) ? $limit : 1000 );
$cl->SetRankingMode ( $ranker );
$res = $cl->Query ( $q, $index );

///////////////////////
// print query results
///////////////////////

//print_r($res);

if ( $res===false )
{
	print "Query failed: " . $cl->GetLastError() . ".\n";

} else
{
	if ( $cl->GetLastWarning() )
		print "WARNING: " . $cl->GetLastWarning() . "\n\n";

	print "Query '$q' retrieved $res[total] of $res[total_found] matches in $res[time] sec.\n";
	print "Query stats:\n";
	if ( is_array($res["words"]) )
		foreach ( $res["words"] as $word => $info )
			print "    '$word' found $info[hits] times in $info[docs] documents\n";
	print "\n";

	if ( is_array($res["matches"]) )
	{
		$n = 1;
		print "Matches:\n";
		foreach ( $res["matches"] as $docinfo )
		{
			print "$n. doc_id=$docinfo[id], weight=$docinfo[weight]";
			foreach ( $res["attrs"] as $attrname => $attrtype )
			{
				$value = $docinfo["attrs"][$attrname];
				if ( $attrtype & SPH_ATTR_MULTI )
				{
					$value = "(" . join ( ",", $value ) .")";
				} else
				{
					if ( $attrtype==SPH_ATTR_TIMESTAMP )
						$value = date ( "Y-m-d H:i:s", $value );
				}
				print ", $attrname=$value";
			}
			print "\n";
			$n++;
		}
	}
}


