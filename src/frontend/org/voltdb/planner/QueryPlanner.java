/* This file is part of VoltDB.
 * Copyright (C) 2008-2013 VoltDB Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with VoltDB.  If not, see <http://www.gnu.org/licenses/>.
 */

package org.voltdb.planner;

import org.hsqldb_voltpatches.HSQLInterface;
import org.hsqldb_voltpatches.HSQLInterface.HSQLParseException;
import org.hsqldb_voltpatches.VoltXMLElement;
import org.voltdb.ParameterSet;
import org.voltdb.VoltType;
import org.voltdb.catalog.Cluster;
import org.voltdb.catalog.Database;
import org.voltdb.compiler.DatabaseEstimates;
import org.voltdb.compiler.DeterminismMode;
import org.voltdb.compiler.ScalarValueHints;
import org.voltdb.planner.ParsedSelectStmt.ParsedColInfo;
import org.voltdb.plannodes.AbstractPlanNode;
import org.voltdb.plannodes.NodeSchema;
import org.voltdb.plannodes.SchemaColumn;
import org.voltdb.plannodes.SendPlanNode;

/**
 * The query planner accepts catalog data, SQL statements from the catalog, then
 * outputs the plan with the lowest cost according to the cost model.
 *
 */
public class QueryPlanner {
    String m_sql;
    String m_stmtName;
    String m_procName;
    HSQLInterface m_HSQL;
    DatabaseEstimates m_estimates;
    Cluster m_cluster;
    Database m_db;
    String m_recentErrorMsg;
    PartitioningForStatement m_partitioning;
    int m_maxTablesPerJoin;
    AbstractCostModel m_costModel;
    ScalarValueHints[] m_paramHints;
    String m_joinOrder;
    DeterminismMode m_detMode;
    PlanSelector m_planSelector;

    // generated by parse(..)
    VoltXMLElement m_xmlSQL = null;
    ParameterizationInfo m_paramzInfo = null;

    // generated by plan(..)
    boolean m_hasExceptionWhenParameterized = false;
    int m_adhocUserParamsCount = 0;

    /**
     * Initialize planner with physical schema info and a reference to HSQLDB parser.
     *
     * @param sql Literal SQL statement to parse
     * @param stmtName The name of the statement for logging/debugging
     * @param procName The name of the proc for logging/debugging
     * @param catalogCluster Catalog info about the physical layout of the cluster.
     * @param catalogDb Catalog info about schema, metadata and procedures.
     * @param partitioning Describes the specified and inferred partition context.
     * @param HSQL HSQLInterface pointer used for parsing SQL into XML.
     * @param estimates
     * @param suppressDebugOutput
     * @param maxTablesPerJoin
     * @param costModel The current cost model to evaluate plans with.
     * @param paramHints
     * @param joinOrder
     */
    public QueryPlanner(String sql,
                        String stmtName,
                        String procName,
                        Cluster catalogCluster,
                        Database catalogDb,
                        PartitioningForStatement partitioning,
                        HSQLInterface HSQL,
                        DatabaseEstimates estimates,
                        boolean suppressDebugOutput,
                        int maxTablesPerJoin,
                        AbstractCostModel costModel,
                        ScalarValueHints[] paramHints,
                        String joinOrder,
                        DeterminismMode detMode)
    {
        assert(sql != null);
        assert(stmtName != null);
        assert(procName != null);
        assert(HSQL != null);
        assert(catalogCluster != null);
        assert(catalogDb != null);
        assert(costModel != null);
        assert(catalogDb.getCatalog() == catalogCluster.getCatalog());
        assert(detMode != null);

        m_sql = sql;
        m_stmtName = stmtName;
        m_procName = procName;
        m_HSQL = HSQL;
        m_db = catalogDb;
        m_cluster = catalogCluster;
        m_estimates = estimates;
        m_partitioning = partitioning;
        m_maxTablesPerJoin = maxTablesPerJoin;
        m_costModel = costModel;
        m_paramHints = paramHints;
        m_joinOrder = joinOrder;
        m_detMode = detMode;
        m_planSelector = new PlanSelector(m_cluster, m_db, m_estimates, m_stmtName,
                m_procName, m_sql, m_costModel, m_paramHints, m_detMode,
                suppressDebugOutput);
    }

    /**
     * Parse a SQL literal statement into an unplanned, intermediate representation.
     * This is normally followed by a call to
     * {@link this#plan(AbstractCostModel, String, String, String, String, int, ScalarValueHints[]) },
     * but splitting these two affords an opportunity to check a cache for a plan matching
     * the auto-parameterized parsed statement.
     */
    public void parse() throws PlanningErrorException {
        // reset any error message
        m_recentErrorMsg = null;

        // Reset plan node ids to start at 1 for this plan
        AbstractPlanNode.resetPlanNodeIds();

        // use HSQLDB to get XML that describes the semantics of the statement
        // this is much easier to parse than SQL and is checked against the catalog
        try {
            m_xmlSQL = m_HSQL.getXMLCompiledStatement(m_sql);
        } catch (HSQLParseException e) {
            // XXXLOG probably want a real log message here
            throw new PlanningErrorException(e.getMessage());
        }

        m_planSelector.outputCompiledStatement(m_xmlSQL);
        // System.out.println("DEBUG: SQL IN: " + m_sql + "; SQL OUT:\n" + m_xmlSQL);
    }

    /**
     * Auto-parameterize all of the literals in the parsed SQL statement.
     *
     * @return An opaque token representing the parsed statement with (possibly) parameterization.
     */
    public String parameterize() {
        m_paramzInfo = ParameterizationInfo.parameterize(m_xmlSQL);

        m_adhocUserParamsCount = ParameterizationInfo.findUserParametersRecursively(m_xmlSQL);

        // skip plans with pre-existing parameters and plans that don't parameterize
        // assume a user knows how to cache/optimize these
        if (m_paramzInfo != null) {
            // if requested output the second version of the parsed plan
            m_planSelector.outputParameterizedCompiledStatement(m_paramzInfo.parameterizedXmlSQL);
            return m_paramzInfo.parameterizedXmlSQL.toMinString();
        }

        // fallback when parameterization is
        return m_xmlSQL.toMinString();
    }

    public String[] extractedParamLiteralValues() {
        if (m_paramzInfo == null) {
            return null;
        }
        return m_paramzInfo.paramLiteralValues;
    }

    public ParameterSet extractedParamValues(VoltType[] parameterTypes) throws Exception {
        if (m_paramzInfo == null) {
            return null;
        }
        Object[] paramArray = m_paramzInfo.extractedParamValues(parameterTypes);
        return ParameterSet.fromArrayNoCopy(paramArray);
    }

    /**
     * Get the best plan for the SQL statement given, assuming the given costModel.
     *
     * @return The best plan found for the SQL statement.
     * @throws PlanningErrorException on failure.
     */
    public CompiledPlan plan() throws PlanningErrorException {
        // reset any error message
        m_recentErrorMsg = null;

        // what's going to happen next:
        //  If a parameterized statement exists, try to make a plan with it
        //  On success return the plan.
        //  On failure, try the plan again without parameterization

        if (m_paramzInfo != null) {
            try {
                // compile the plan with new parameters
                CompiledPlan plan = compileFromXML(m_paramzInfo.parameterizedXmlSQL,
                                                   m_paramzInfo.paramLiteralValues);

                VoltType[] paramTypes = plan.parameterTypes();
                if (paramTypes.length <= CompiledPlan.MAX_PARAM_COUNT) {
                    Object[] params = m_paramzInfo.extractedParamValues(paramTypes);
                    plan.extractedParamValues = ParameterSet.fromArrayNoCopy(params);
                    return plan;
                }
                // fall through to try replan without parameterization.
            }
            catch (Exception e) {
                // ignore any errors planning with parameters
                // fall through to re-planning without them

                // note, expect real planning errors ignored here to be thrown again below
                m_recentErrorMsg = null;

                m_hasExceptionWhenParameterized = true;
            }
        }

        // if parameterization isn't requested or if it failed, plan here
        CompiledPlan plan = compileFromXML(m_xmlSQL, null);
        if (plan == null) {
            throw new PlanningErrorException(m_recentErrorMsg);
        }
        return plan;
    }

    /**
     * @return Was this statement planned with auto-parameterization?
     */
    public boolean compiledAsParameterizedPlan() {
        return m_paramzInfo != null;
    }

    public boolean wasBadPameterized() {
        return m_hasExceptionWhenParameterized;
    }

    public int getAdhocUserParamsCount() {
        return m_adhocUserParamsCount;
    }

    private CompiledPlan compileFromXML(VoltXMLElement xmlSQL, String[] paramValues) {
        // Get a parsed statement from the xml
        // The callers of compilePlan are ready to catch any exceptions thrown here.
        AbstractParsedStmt parsedStmt = AbstractParsedStmt.parse(m_sql, xmlSQL, paramValues, m_db, m_joinOrder);
        if (parsedStmt == null)
        {
            m_recentErrorMsg = "Failed to parse SQL statement: " + m_sql;
            return null;
        }
        if ((parsedStmt.tableList.size() > m_maxTablesPerJoin) && (parsedStmt.joinOrder == null)) {
            m_recentErrorMsg = "Failed to parse SQL statement: " + m_sql + " because a join of > 5 tables was requested"
                               + " without specifying a join order. See documentation for instructions on manually" +
                                 " specifying a join order";
            return null;
        }

        m_planSelector.outputParsedStatement(parsedStmt);

        // Init Assembler. Each plan assembler requires a new instance of the PlanSelector
        // to keep track of the best plan
        PlanAssembler assembler = new PlanAssembler(m_cluster, m_db, m_partitioning, (PlanSelector) m_planSelector.clone());
        // find the plan with minimal cost
        CompiledPlan bestPlan = assembler.getBestCostPlan(parsedStmt);

        // make sure we got a winner
        if (bestPlan == null) {
            m_recentErrorMsg = assembler.getErrorMessage();
            if (m_recentErrorMsg == null) {
                m_recentErrorMsg = "Unable to plan for statement. Error unknown.";
            }
            return null;
        }

        if (bestPlan.readOnly == true) {
            SendPlanNode sendNode = new SendPlanNode();
            // connect the nodes to build the graph
            sendNode.addAndLinkChild(bestPlan.rootPlanGraph);
            // this plan is final, generate schema and resolve all the column index references
            bestPlan.rootPlanGraph = sendNode;
        }

        // Execute the generateOutputSchema and resolveColumnIndexes Once from the top plan node for only best plan
        bestPlan.rootPlanGraph.generateOutputSchema(m_db);
        bestPlan.rootPlanGraph.resolveColumnIndexes();
        if (bestPlan.selectStmt != null) {
            checkPlanColumnLeakage(bestPlan, bestPlan.selectStmt);
        }

        // Output the best plan debug info
        assembler.finalizeBestCostPlan();

        // reset all the plan node ids for a given plan
        // this makes the ids deterministic
        bestPlan.resetPlanNodeIds();

        // split up the plan everywhere we see send/recieve into multiple plan fragments
        Fragmentizer.fragmentize(bestPlan, m_db);
        return bestPlan;
    }

    private void checkPlanColumnLeakage(CompiledPlan plan, ParsedSelectStmt stmt) {
        NodeSchema output_schema = plan.rootPlanGraph.getOutputSchema();
        // Sanity-check the output NodeSchema columns against the display columns
        if (stmt.displayColumns.size() != output_schema.size())
        {
            throw new PlanningErrorException("Mismatched plan output cols " +
            "to parsed display columns");
        }
        for (ParsedColInfo display_col : stmt.displayColumns)
        {
            SchemaColumn col = output_schema.find(display_col.tableName,
                                                  display_col.tableAlias,
                                                  display_col.columnName,
                                                  display_col.alias);
            if (col == null)
            {
                throw new PlanningErrorException("Mismatched plan output cols " +
                                                 "to parsed display columns");
            }
        }
        plan.columns = output_schema;
    }

}
