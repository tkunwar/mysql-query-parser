/**
 * @file qparser.cpp
 * @author Tej
 * @brief Create a query parser for mysql which finds out the tables and
 * corresponding column names present in a query.
 *
 * Note: use C++ strings in all your internal functions and methods. If you
 * ever write a .dll or .so, use C strings in your public (dll/so-exposed)
 * functions.
 */

#include <algorithm>
#include <array>
#include <string.h>
#include <iostream>
#include <string>
#include <stack>
#include <list>
#include <regex>

typedef enum {
	NONE, SELECT, FROM, WHERE
} token_state_t;

typedef struct lookup_table_for_name_alias_t {
	std::string table_name;
	std::string alias_name;
} lookup_table_for_name_alias_t;

struct query_state_t {
	std::list<std::string> table_name_list;
	token_state_t current_state;
	token_state_t previous_state;
	bool select_triggered_query_state_change;
};
struct TblColList {
	std::list<std::string> mTblNameList;
	std::list<std::string> mTblColNameList;
};

/**
 * Global flag which denotes the presence of UNION or SELECT keyword in the
 * current stream. This will lead to clearing of table_name_list which will
 * contain list of table_names encountered so far.After clearing the table_name_list
 * it is set to false again.
 */
bool state_reset_needed = false;

void toggle_state_reset() {
	state_reset_needed = (state_reset_needed == true) ? false : true;
}

std::string convert_to_uppercase(std::string token) {
	std::string tmp = "";
	tmp = token;
	std::transform(tmp.begin(), tmp.end(), tmp.begin(), ::toupper);
	return tmp;
}
/**
 * @brief Sets the state depending upon token scanned in the input. And returns
 * 		true/false indicating whether the state changed or not. True meaning
 * 		state changed while false otherwise.
 * @param token Token which will be examined.
 * @param current_state The current state of the program
 * @param previous_state The previous state of the program.
 * @return Returns true/false indicating whether current token triggered a state
 * 			change.
 */
bool set_state(std::string &token, token_state_t *current_state,
		token_state_t *previous_state) {
	std::string token_in_cap = convert_to_uppercase(token);
	int cur_state = *current_state;

	if (token_in_cap == "SELECT" or token_in_cap == "UNION") {
		/*
		 * if state is SELECT then both states will reset
		 * UNION will any way be followed by a SELECT so action is same
		 */
		*previous_state = *current_state = SELECT;
		state_reset_needed = true;
		return true;
	} else if (token_in_cap == "FROM" or token_in_cap == "JOIN") {
		/*
		 * JOIN and FROM lists table names:
		 * select ss.secondaryKeyword FROM site s  INNER JOIN site_state st
		 */
		*previous_state = *current_state;
		*current_state = FROM;
	} else if (token_in_cap == "WHERE" or token_in_cap == "ON"
			or token_in_cap == "BY") {
		/*
		 * ON| WHERE| BY: give columns names usually in composite manner
		 * BY is part of 'ORDER BY' clause
		 * ..JOIN site_seo ss ON ss.siteId = s.siteId LEFT JOIN site_noalert na ON na.site = s.siteId
		 */
		*previous_state = *current_state;
		*current_state = WHERE;
	}

	if (*current_state == cur_state) {
		return false;
	}
	return true;
}
/**
 * @brief Check if a token is reserved most importantly whether it can trigger
 * 			state change.
 * @param token Token which is to be checked
 * @return Return true/false depending upon whether this token is a reserved
 * 			one or not
 */
bool is_token_reserved(std::string token) {
	/*
	 * whenever comparing for keywords always, uppercase the token which is to be
	 * compared
	 */
	std::string token_in_cap = convert_to_uppercase(token);

	//list of reserved keywords
	const char *keywords[] = { "SELECT", "FROM", "WHERE", "GROUP", "BY",
			"HAVING", "AND", "OR", "NOT", "INNER", "OUTER", "ON", "JOIN",
			"ORDER", "LIMIT", "ASC", "DESC", "ALL", "LEFT", "RIGHT", "UNION",
			"LIKE", "MAX", "IN", "IS", "NULL", "NOW" };
	unsigned int number_of_keywords = 27;

	for (unsigned int i = 0; i < number_of_keywords; i++) {
		if (token_in_cap == keywords[i]) {
			return true;
		}
	}
	return false;
}
/**
 * @brief Checks if the passed token is an operator
 * @param token The token which is to be checked
 * @return true/false indicating whether this token is an operator or not.
 */
bool is_token_operator(std::string token) {
	char operators[] = { '+', '-', '\\', '*', '=', '.', '<', '>', ':','!', '\0' };
	unsigned int number_of_operators = strlen(operators);
	for (unsigned int i = 0; i < number_of_operators; i++) {
		if (token[0] == operators[i])
			return true;
	}
	return false;
}
/**
 * @brief A high level routine which checks if a token is valid name for
 * 			a table or column.
 *
 * For e.g. after we have filtered string literals (those
 * in single and double quotes), removed spaces, we have tokens. Tokens
 * can be anything which means something to our program. For e.g.
 * a comma,operators and keywords etc.  However when we are
 * filtering table and columns the token can not have comma,operators or
 * reserved keywords so we must filter them out. This routine just does
 * that.
 *
 * Also as a side note when this routine returns false, the caller will do a
 * token_pushback.
 *
 * @param token The token which is to be tested.
 * @return true if token can form a proper table/column name , false otherwise.
 */
bool is_valid_tblcol_name(std::string &token) {
	/*
	 * what we cant have ?
	 * we cant have operators,reserved keywords ? We cant have comma and semi-colons.
	 * we can only have c type variable names.
	 */
	if (token == "")
		return false;

	if (is_token_operator(token) or is_token_reserved(token)) {
		return false;
	}
	//no number as the first element
	if (isdigit(token[0]))
		return false;

	// comma and semicolon are also not in this category
	if (token == "," or token == ";")
		return false;
	return true;
}
/**
 * @brief Read next token from input buffer.
 * @param input The buffer from which a token will be searched.
 * @param index The index from where next token will be scanned. This will be
 * 			updated in the process.
 * @return Return the scanned token.
 */
std::string get_next_token(std::string *input, unsigned int* index) {
	std::string token = "";
	/*
	 * token_delmiters: define a point until when whatever we have read in
	 * buffer is a token. Note that they will not be part of any token unless
	 * token is a comment or a string as enclosed in ''(single quotes) and ""
	 * (double quotes). Currently token delimiters are space and comma ' '
	 * and ',' .
	 *
	 * Note: No matter what we will always advance index such that it will point
	 * to next character in the stream ahead of
	 */
	/*
	 * token_separators: we buffer elements from input stream until one of
	 * them is encountered. Note that they themselves are tokens as well.
	 */
	char token_separators[] = { ',', '+', '.', '-', '*', '\\', '=', '(', ')',
			'<', '>', ';', ':','!', '\0' };
	int number_of_token_separators = strlen(token_separators);
	//find tokens
	for (; *index < (*input).length(); (*index)++) {
		/*
		 * 1. detect strings enclosed in single quotes. Note that token obtained
		 *  in this case is inclusive of opening and closing quotes.
		 */
		if ((*input)[*index] == '\'') {
			token = token + (*input)[*index];
			(*index)++;

			while (((*input)[*index] != '\'') and (*index < (*input).length())) {
				token = token + (*input)[*index];
				(*index)++;
			}
			token = token + (*input)[*index];
			(*index)++;
			return token;
		}

		/*
		 * 2. detect strings enclosed in double quotes. Token is inclusive of
		 *  opening and closing double quotes.
		 */
		if ((*input)[*index] == '\"') {
			token = token + (*input)[*index];
			(*index)++;
			while ((*input)[*index] != '\"' and (*index < (*input).length())) {
				token = token + (*input)[*index];
				(*index)++;
			}
			token = token + (*input)[*index];
			(*index)++;
			return token;
		}
		/*
		 * 3.detect token which begin with '`' backtick character. These are
		 * legal tokens in mysql. we will consider inclusive of opening
		 * and end backticks
		 */
		if ((*input)[*index] == '`') {
			token = token + (*input)[*index];
			(*index)++;
			while ((*input)[*index] != '`' and (*index < (*input).length())) {
				token = token + (*input)[*index];
				(*index)++;
			}
			token = token + (*input)[*index];
			(*index)++;
			return token;
		}
		/*
		 * 4. find token separators. they are tokens as well. However special
		 * about them is that the moment they are encountered we return. So
		 * a token separator is 'alone' .
		 */
		for (int i = 0; i < number_of_token_separators; i++) {
			if ((*input)[*index] == token_separators[i]) {
				//if we already have something in token then we need to return.
				if (token.length() != 0) {
					return token;
				}

				token = token + (*input)[*index];
				(*index)++;
				return token;
			}
		}
		/*
		 * 6. default case. if none of above conditions were satisfied then
		 * this character is a part of token.
		 *
		 * Also note that we will eat spaces in beginning of a token not after it.
		 * so the moment we find a space after we have filled something in token
		 * we will return .
		 */
		if ((*input)[*index] != ' ') {
			token = token + (*input)[*index];
		}
		if ((*input)[*index + 1] == ' ' and token != "") {
			(*index)++;
			return token;
		}

		//5. Eat out spaces/delimiters.
		if ((*input)[*index] == ' ') {
			(*index)++;
			while (((*input)[*index] == ' ') and (*index < (*input).length())) {
				(*index)++;
			}
			/*
			 * stop when we have a token. but we were eating out spaces. However
			 * if we have not read anything then dont break. continue as we have
			 * still to find a valid token.
			 */
			if (token != "") {
				return token;
			} else {
				/*
				 * this character is not space, neither is it a separator but still
				 * has not been stored in token. however this char could be ' or "
				 * which require special treatment. so go up but ensure that
				 * the pointer is not incremented.
				 */
				(*index)--;
				continue;
			}
		}

	}
	return token;
}

/**
 * @brief Checks if a passed string is a number or not.
 * @param s The passed string which is to be checked .
 * @return True/false indicating whether the passed string is a number or not.
 */
bool is_number(const std::string& s) {
	std::string::const_iterator it = s.begin();
	while (it != s.end() && std::isdigit(*it))
		++it;
	return !s.empty() && it == s.end();
}
/**
 * @brief Skips tokens which will not form a column-name or a table name.
 * @param token The token which is to be validated.
 * @return true/false indicating whether this token should be skipped
 */
bool is_valid_token(std::string &token) {
	//tokens beginning with single quotes,double quotes and empty tokens are
	// all invalid tokens
	if (token[0] == '\'' or token[0] == '"' or token == "") {
		return false;
	}
	if (is_number(token)) {
		return false;
	}
	return true;
}

/**
 * @brief Ensure that passed token is in proper form and if needed modify it.
 * @param token The modified token which is in a valid form.
 */
void sanitize_token(std::string *token) {
	std::string tmp;
	std::string::iterator iter;
	if ((*token)[0] == '"' or (*token)[0] == '`') {
		for (iter = (*token).begin() + 1; iter != (*token).end() - 1; iter++) {
			tmp = tmp + *iter;
		}
	}
	*token = tmp;
}
/**
 * @brief Prints a list of strings.
 * @param mylist The list of strings which is to be printed.
 */
void print_list(std::list<std::string> &mylist) {
	for (std::list<std::string>::iterator it = mylist.begin();
			it != mylist.end(); it++) {
		std::cout << *it << std::endl;
	}
}
/**
 * @brief Prints the elements of lookup_table_for_name_alias type list
 * @param mylist The list which contents will be printed.
 */
void print_lookup_table(std::list<lookup_table_for_name_alias_t> &mylist) {
	for (std::list<lookup_table_for_name_alias_t>::iterator it = mylist.begin();
			it != mylist.end(); it++) {
		std::cout << (it->table_name) << " " << (it->alias_name) << std::endl;
	}
}
/**
 * @brief Lookup in the table_name and alias_table_name list to find the table_name for
 * 		the queried alias_name.
 *
 * @param alias_name Whose equivalent table_name is to be searched.
 * @return The table_name of type <string>
 */
std::string find_table_name_of_alias_tblname(
		std::list<lookup_table_for_name_alias_t> &mylist,
		std::string alias_name) {
	if (alias_name == "") {
		//if alias name is empty then return last table name;
		if (!mylist.empty()) {
			return ((mylist.back().table_name));
		}
	}
	for (std::list<lookup_table_for_name_alias_t>::iterator it = mylist.begin();
			it != mylist.end(); it++) {
		if (it->alias_name == alias_name) {
			return it->table_name;
		}
	}
	// if we could not find a table_name for the alias name asked then it could be
	// of form ..from t1,t2 where t1.x > t2.y . in this case t1 is the name of
	// table itself so return the alias name itself
	return alias_name;
}
/**
 * @brief Stores table name in a list ensuring that no duplicate entries are
 * 			saved to the list.
 * @param mTblNameList The list where the passed table_name, if unique, will be
 * 			stored.
 * @param table_name The name of table which is to be stored.
 */
void store_table_name_uniquely(std::list<std::string>& mTblNameList,
		std::string table_name) {
	for (std::list<std::string>::iterator it = mTblNameList.begin();
			it != mTblNameList.end(); it++) {
		if (*it == table_name)
			return;
	}
	mTblNameList.push_back(table_name);
}
/**
 * @brief Stores table_col_name in the specified list with no duplicate entries.
 * @param mTblColNameList The list where entries will be stored.
 * @param tblcol_name The token which is to be stored.
 */
void store_table_col_name_uniquely(std::list<std::string>& mTblColNameList,
		std::string tblcol_name) {
	//tblcol_name is recieved in format <table_name>.<col_name>
	for (std::list<std::string>::iterator it = mTblColNameList.begin();
			it != mTblColNameList.end(); it++) {
		if (*it == tblcol_name) {
			return;
		}
	}
	mTblColNameList.push_back(tblcol_name);
}
/**
 * @brief Prints what we have found in the current query.
 * @param res The result structure of type TblColList *
 */
void print_final_result(struct TblColList *res) {
	std::cout << "Table name list: ";
	for (std::list<std::string>::iterator it = res->mTblNameList.begin();
			it != res->mTblNameList.end(); it++) {
		std::cout << "["<<*it <<"]"<< " ";
	}
	std::cout << std::endl;
	std::cout << "Table_name with col_name: ";
	for (std::list<std::string>::iterator it = res->mTblColNameList.begin();
			it != res->mTblColNameList.end(); it++) {
		std::cout <<"[" <<*it<<"]" << " ";
	}
	std::cout << std::endl;
}
/**
 * @brief A wrapper routine for get_next_token(). Reads  next token from query
 * 		 using get_next_token() and further ensures that the token read is a
 * 		 valid one.
 * @param query The query from where next token will be read.
 * @param index The index in the passed query from where next token will be
 * 			scanned.
 * @return The token that we have just read.
 */
std::string get_next_valid_token(std::string *query, unsigned int* index) {
	std::string current_token = get_next_token(query, index);
	std::string tmp = "";
	//check if this token is a valid one
	while (!is_valid_token(current_token) and *index < query->length()) {
		current_token = get_next_token(query, index);
	}
	//how did we get out ? was it because we got a valid token or because end
	// of query reched ?
	if (*index >= query->length() and !is_valid_token(current_token)) {
		return "";
	}
	// remove tokens enclosed in backticks if any
	if (current_token[0] == '`') {
		sanitize_token(&current_token);
	}
	// if token is CONCAT then eat out everything until a ')' is found
	if (convert_to_uppercase(current_token) == "CONCAT") {
		current_token = get_next_token(query, index);
		while (current_token != ")" and *index < query->length()) {
			current_token = get_next_token(query, index);
		}
		if (*index < query->length()) {
			//this is bad as even though closing ')' have been encountered
			// we still need a valid token
			return "";
		}
		//if we are here then current_token is ")" now read next_token
		// so far we have only eaten CONCAT block. it's not for sure that next
		// block is a valid one.
		// is this recursive block correct ?
		current_token = get_next_valid_token(query, index);
		//ensure that it's valid one
//		while (!is_valid_token(current_token) and *index < query->length() ) {
//				current_token = get_next_token(query, index);
//			}
	} else if (convert_to_uppercase(current_token) == "MAX") {
		/*
		 * MAx(coulmn_name) is a keyword that gives a column name in round
		 * brackets.
		 */
		tmp = get_next_token(query, index); //must be a (
		if (tmp != "(") {
			std::cerr << "No '(' after MAX at pos: " << *index << std::endl;
			return "";
		}
		//now read the actual col name
		current_token = get_next_token(query, index);
		//bypass the closing ')'
		tmp = get_next_token(query, index); //must be a (
		if (tmp != ")") {
			std::cerr << "No ')' after MAX at pos: " << *index << std::endl;
			return "";
		}
	}
	return current_token;
}
/**
 * @brief If a desired token is not found in the stream then a caller
 * routine can use this routine to move back the stream pointer backward.
 *
 * This is useful when at a particular point, a sub section of code may not
 * handle a given token however at some other portion of code may handle it
 * properly. So the first section of code can push this token back and
 * let the upper level (a higher scope 'while' loop) code handle the token.
 * This will allow cleaner code division.
 *
 * Also note that we not pushing token back to any stream, we are only moving
 * the stream pointer given by stream_index in backward direction.
 *
 * @param token The token which will be pushed back.
 * @param stream_index The index which points to the stream we are processing.
 */
void pushback_token_to_stream(std::string *token, unsigned int *stream_index) {
	*stream_index = *stream_index - (token->length());
}
/**
 * @brief The main routine which accepts a SQL query and returns a list of type
 * 			TblColList which will contain list of all table and column names
 * 			referenced in the given query.
 * @param queryStr The query which is to be looked into.
 * @return A list of results.
 */
struct TblColList* ProcessQuery(std::string queryStr) {
	std::string current_token, previous_token, next_token;
	unsigned int index = 0;
	std::list<std::string> table_name_list; //store list of tables in current state
	// a SELECT/UNION will reset it.
	token_state_t current_state = NONE, previous_state = NONE;
	std::string table_name, col_name;

	std::list<lookup_table_for_name_alias_t> lookup_table_list;
	lookup_table_for_name_alias_t lookup_element;

	/*
	 * we will use stack where we will save the table_name_list the moment
	 * we encounter a opening round bracket. We dont need to save pRes as
	 * this stores relationship already established between column name and tables.
	 * This is in a way immutable once we the values have been stored. values
	 * such as current_token, next_token and index are all either changing and thus
	 * have state for that iteration only or their linear growth is valid even
	 * in a subquery (for index).
	 *
	 */
	struct query_state_t query_state;
	std::stack<struct query_state_t> query_state_stack;

	struct TblColList *pRes = new TblColList;

	while (index < queryStr.length()) {
		col_name = "";
		current_token = "";
		current_token = get_next_valid_token(&queryStr, &index);

		//have we reached end of stream
		if (current_token == "") {
			//no matter what we must end processing. How could we get an empty token ?
			return pRes;
		}

		if (current_token == "(") {
			/*
			 * when a opening '(' is encountered in the stream, it will not
			 * necessarily mean beginning of a sub-query. It can involve expressions
			 * like:
			 * ..from coupon c where c.id=5 and (c.roll>9)
			 * If we do a state save the moment we encounter "(" then alias c cant be
			 * looked up therefore we will do a state save only when we encounter
			 * SELECT after '(' .
			 *
			 * Also when we encounter a '(' NOT followed by a select then we will
			 * do the state save but we will not reset the state. So we will push
			 * current state the moment we find '(' but we will reset the state only
			 * when the next token is SELECT.
			 *
			 * Another option could have been that we save state only when '(' is
			 * followed by SELECT. But when we encounter the closing bracket ')'
			 * what do we pop off the stack ?
			 * (Well it could be simple pop if u can otherwise ignore. It would only
			 * mean that the opening ( for this closing ) did not mark a subquery.)
			 * Not a good option since for mal-formed query if there are misplaced
			 * closing brackets then wrong state will get popped off at wrong time .
			 * (optimal but does not work)
			 */

			next_token = "";
			next_token = get_next_valid_token(&queryStr, &index);

			//create an empty query_state variable
			query_state.current_state = NONE;
			query_state.previous_state = NONE;
			query_state.table_name_list.clear();
			query_state.select_triggered_query_state_change = false;

			//save the state
			query_state.current_state = current_state;
			query_state.previous_state = previous_state;
			query_state.table_name_list = table_name_list;

			//state save only when SELECT is the next token
			if (convert_to_uppercase(next_token) == "SELECT") {
				query_state.select_triggered_query_state_change = true;
				query_state_stack.push(query_state);

				//also  reset the state
				table_name_list.clear();
				current_state = NONE;
				previous_state = NONE;
			}
			//also push back this token
			pushback_token_to_stream(&next_token, &index);
			continue;
		}
		if (current_token == ")") {
			//now time to pop back what we stored in stack
			if (query_state_stack.empty()) {
//				std::cerr << "State stack empty ! Inconsistent state !!"
//						<< std::endl;
//				return pRes;
				continue;
			}

			if (query_state_stack.top().select_triggered_query_state_change
					== true) {
				// if the state saved at stack was triggered by SELECT then only
				// do a state save and pop
				//pop and save state into current variables
				table_name_list = (query_state_stack.top().table_name_list);
				current_state = (query_state_stack.top()).current_state;
				previous_state = (query_state_stack.top()).previous_state;

				//pop the top
				query_state_stack.pop();

			}
			continue;
		}
		// see if this token triggers a state change
		if (set_state(current_token, &current_state, &previous_state) == true) {
			continue;
		}

		/*
		 * if a state reset needed because keyword SELECT/UNION has been
		 * encountered in the input stream, then perform a state reset. After
		 * resetting table_name_list toggle state_reset_needed flag.
		 */
		if (state_reset_needed) {
			table_name_list.clear();
			toggle_state_reset();
		}
		/*
		 * when being in a state, if a reserved token is encountered and
		 * if code at that position can not handle it then it must do
		 *  a pushback followed a continue. We will handle reserved keywords
		 *  or unhandled tokens here.
		 *  Right now we are not handling most of the reserved tokens or operators
		 *  in this block. so continue
		 */
		if (is_token_reserved(current_token)
				or is_token_operator(current_token)) {
			// deal with token
			continue;
		}

		if (current_token == "," or current_token == ";") {
			continue;
		}

		//process state SELECT
		if (current_state == SELECT) {

		}
		// process state FROM
		if (current_state == FROM) {
			/*
			 * we can look for columns in FROM state
			 * 1. .. select name,roll from table_t1,table_t2
			 * 		// table_names separated by comma
			 * 2. .. select name,roll from table1 where roll>9
			 * 		// table names followed by reserved keywords
			 * 3. .. select a,b,c from table1 as t1,table2 as t2
			 * 		// table_name with alias_name separated by 'AS'
			 * 4. .. select t1.name,t2.roll from table1 t1,table2 t2
			 * 		// table_name with alias separated by space
			 */
			lookup_element.alias_name = "";
			lookup_element.table_name = "";

			if (!is_valid_tblcol_name(current_token)) {
				pushback_token_to_stream(&current_token, &index);
				continue;
			}

			next_token = "";
			next_token = get_next_valid_token(&queryStr, &index);
			/*
			 * Next token can be 'AS' or an alias name. For all other values
			 * of next_tokens, it must be pushed back to stream
			 */
			if ((is_valid_tblcol_name(next_token) == false)) {
				/*
				 * first and second case
				 * token_reserved will be when we have single table only.
				 * should AND,OR,NOT be part of reserved_tokens or operators ?
				 */
				table_name_list.push_back(current_token);

				lookup_element.table_name = current_token;
				lookup_table_list.push_back(lookup_element);

				//next_token may be reserved see if it triggers state change
				pushback_token_to_stream(&next_token, &index);
				continue;

			} else if (next_token == "AS" or next_token == "as") {
				// third case , then do one more lookahead
				table_name_list.push_back(current_token);

				//get next token
				next_token = get_next_valid_token(&queryStr, &index);
				if (!is_valid_tblcol_name(next_token)) {
					//this is bad
					std::cerr
							<< "Expected a valid <column_name> after AS before : "
							<< next_token << std::endl;
					return pRes;
				}
				//save the table_name and col_names
				lookup_element.table_name = current_token;
				lookup_element.alias_name = next_token;
				lookup_table_list.push_back(lookup_element);
				continue;
			} else {
				//fourth case
				table_name_list.push_back(current_token);
				lookup_element.table_name = current_token;
				lookup_element.alias_name = next_token;
				lookup_table_list.push_back(lookup_element);
			}

		} else if (current_state == WHERE) {
			/*
			 * what about queries where col name is referenced in a non-composite
			 * relationship ? for e.g 'select rollno from class where rollno >9'.
			 * since we are processing only queries where cols and tables will
			 * be referenced not selected, then we might not handle this case.
			 */
			// reject tokens that we might not need
			// for now we will reject any reserved keyword or operator
			if (!is_valid_tblcol_name(current_token)) {
				pushback_token_to_stream(&current_token, &index);
				continue;
			}

			next_token = "";
			next_token = get_next_valid_token(&queryStr, &index);
			if (next_token == ".") {
				//case where composite col name and table name will be found
				next_token = get_next_valid_token(&queryStr, &index);
				if (!is_valid_tblcol_name(next_token)) {
					//thats bad
					std::cerr << "Expected valid token after '.' near " << index
							<< std::endl;
					return pRes;
				}
				/*
				 * checks could be put here to ensure that next_token is a valid token
				 * as a matter of all places where we are doing lookahead, this
				 * should be checked.
				 */

				//current_token could be alias so lets get its table name
				table_name = find_table_name_of_alias_tblname(lookup_table_list,
						current_token);
				if (!is_valid_tblcol_name(table_name)) {
					//its an error -- will not happen since we will get back alias name
					// in cases where we dont find a suitable table_name for alias_name
				} else {
					store_table_name_uniquely(pRes->mTblNameList, table_name);
					std::string tmp = table_name + "." + next_token;
					store_table_col_name_uniquely(pRes->mTblColNameList, tmp);
				}
			} else {
				/*
				 *  =========== NON_STANDARD BEHAVIOUR ===========
				 * case where we have non-composite and possibly single col.
				 * When in WHERE clause more than one columns are referenced then
				 * they will always use '.' to denote table_name or table_name_alias
				 * with col_name . For.eg. select * from table1,table2 where table1.id >5 and table2.id <5;
				 * And when there are no colms with '.' separating the col and table_name
				 * then it means we have single table only.
				 *
				 * Therefore, this token is the col_name beglonging
				 * to this case.
				 *
				 * Resolving the ambiguity: IDB-4122
				 * ----------------------
				 *
				 * For single table case: all cols will be considered as referenced. However for
				 * multi-table non-composite columns , we will list them without any relationship.
				 */
				if (table_name_list.size() == 1) {
					/*
					 * case where we have single table name but may have
					 * mutliple cols.
					 */
					table_name = table_name_list.front();
					store_table_name_uniquely(pRes->mTblNameList, table_name);
					std::string tmp = table_name + "." + current_token;
					store_table_col_name_uniquely(pRes->mTblColNameList, tmp);
				} else {
					/*
					 * case where we have more than one tables --
					 * ambiguity IDB-4122
					 */
					for (std::list<std::string>::iterator it =
							table_name_list.begin();
							it != table_name_list.end(); it++) {
						store_table_name_uniquely(pRes->mTblNameList, *it);
					}

					std::string tmp = current_token;
					store_table_col_name_uniquely(pRes->mTblColNameList, tmp);
				}
			}
		}
	}
	return pRes;
}
int main(int argc, char *argv[]) {
	std::string query;
	struct TblColList *res = NULL;
	while (!std::cin.eof()) {
		getline(std::cin, query);
		if (query == "")
			continue;
		std::cout << "Parsing query: " << query << "\n" << std::endl;
		res = ProcessQuery(query);
		print_final_result(res);
		std::cout << std::endl;
	}
	exit(EXIT_SUCCESS);
}

