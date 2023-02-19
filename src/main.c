#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>


/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)

#define MSG_SIZE 32

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

/* receive buffer used in UART ISR callback */
static char rx_buf[MSG_SIZE];
static int rx_buf_pos;

/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		return;
	}

	/* read until FIFO empty */
	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
			/* terminate string */
			rx_buf[rx_buf_pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

			/* reset the buffer (it was copied to the msgq) */
			rx_buf_pos = 0;
		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
	}
}

/*
 * Print a null-terminated string character by character to the UART interface
 */
void print_uart(char *buf)
{
	int msg_len = strlen(buf);

	for (int i = 0; i < msg_len; i++) {
		uart_poll_out(uart_dev, buf[i]);
	}
}

/* Parser struct for keeping track of consumed tokens */
typedef struct parserTok {
	char* tokens;
	int numTokens;
	int index;
} parserTok;

/* Tree struct to hold expression */
typedef struct expressionTree {
	char type;
	int val;
	struct expressionTree* left;
	struct expressionTree* right;
} expressionTree;

#define VALID_TOKENS "+-*/0123456789()"

char* tokenize(char* str);
expressionTree* parse(char* tokens);
int calculate(expressionTree* expression);

expressionTree* parseAddition(parserTok* parser);
expressionTree* parseMultiplication(parserTok* parser);
expressionTree* parseParenthesis(parserTok* parser);
expressionTree* parseNumber(parserTok* parser);

expressionTree* createExpressionTree(char type, int value, expressionTree* left, expressionTree* right);
void freeExpressionTree(expressionTree* expression);

void main(void)
{
	char tx_buf[MSG_SIZE];

	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
		return;
	}

	/* configure interrupt and callback to receive data */
	int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);

	if (ret < 0) {
		if (ret == -ENOTSUP) {
			printk("Interrupt-driven UART API support not enabled\n");
		} else if (ret == -ENOSYS) {
			printk("UART device does not support interrupt-driven API\n");
		} else {
			printk("Error setting UART callback: %d\n", ret);
		}
		return;
	}
	uart_irq_rx_enable(uart_dev);

	print_uart("Hello! I'm a simple calculator running on Zephyr.\n");
	print_uart("Give me an expression or type 'exit' to leave and press enter:\n");

	/* indefinitely wait for input from the user */
	while (k_msgq_get(&uart_msgq, &tx_buf, K_FOREVER) == 0) {
		if(!strcmp("exit", tx_buf)) break;

		print_uart(tx_buf);
		print_uart(" ");

		char myMsg[MSG_SIZE];

		char* tokens = tokenize(tx_buf);

		if (tokens == NULL) print_uart("\ninvalid input\r\n");
		else {
			expressionTree* expression = parse(tokens);
			int val = calculate(expression);

			sprintf(myMsg, "%d\r\n", val);
			print_uart(myMsg);

			freeExpressionTree;
		}
	}

	printf("Quitting...\n");
	printf("To exit from QEMU enter: 'CTRL+a, x'\n");

	return 0;
}

/* Return a string of valid tokens */
char* tokenize(char* str) {
	char* tokens = malloc(sizeof(char) * MSG_SIZE);

	int index = 0;

	// Iterate through the input string and add only valid tokens (chars) into tokens string
	for (int i = 0; i < strlen(str); i++) {
		if (strchr(VALID_TOKENS, str[i])) {
			tokens[index] = str[i];
			index++;
		}

		// Ignore spaces and equal sign
		else if (str[i] == ' ' || str[i] == '=') continue;

		else return NULL;
	}

	tokens[index] = '\0';
	return tokens;
}

/* Parse tokens into an expression tree */
expressionTree* parse(char* tokens) {
	parserTok* parser = malloc(sizeof(parserTok));

	parser->tokens = tokens;
	parser->numTokens = strlen(tokens);
	parser->index = 0;

	// Start with lowest priority operation
	expressionTree* expression = parseAddition(parser);

	free(parser->tokens);
	free(parser);

	return expression;
}

int calculate(expressionTree* expression) {
	/* If the expression type is number, return its value */
	if (expression->type == 'n') return expression->val;

	int left = calculate(expression->left);
	int right = calculate(expression->right);

	if (expression->type == '+')
		return left + right;
	else if (expression->type == '-')
		return left - right;
	else if (expression->type == '*')
		return left * right;
	else if (expression->type == '/')
		return left / right ? right : 0;
	
	return 0;
}

/* addition = multiplication (('+' | '-') multiplication) */
expressionTree* parseAddition(parserTok* parser) {
	expressionTree* expression = parseMultiplication(parser);

	while (parser->index < parser->numTokens && (parser->tokens[parser->index] == '+' || parser->tokens[parser->index] == '-')) {
		char type = parser->tokens[parser->index];

		parser->index++;

		expressionTree* rightExpression = parseMultiplication(parser);

		expression = createExpressionTree(type, 0, expression, rightExpression);
	}

	return expression;
}

/* multiplication = parenthesis (('*' | '/') parenthesis) */
expressionTree* parseMultiplication(parserTok* parser) {
    expressionTree* expression = parseParenthesis(parser);
    
    while (parser->index < parser->numTokens && (parser->tokens[parser->index] == '*' || parser->tokens[parser->index] == '/')) {
        char type = parser->tokens[parser->index];

        parser->index++;

        expressionTree* rightExpression = parseParenthesis(parser);

        expression = createExpressionTree(type, 0, expression, rightExpression);
    }
    
    return expression;

}

/* parenthesis = number | leftParenthesis addition rightParenthesis */
expressionTree* parseParenthesis(parserTok* parser) {
	expressionTree* expression;

	if (parser->tokens[parser->index] == '(') {

		parser->index++;

		expression = parseAddition(parser);

		if (parser->tokens[parser->index] == ')') {
			parser->index++;
		}
	}

	// If there is only a number
	else expression = parseNumber(parser);

	return expression;
}

expressionTree* parseNumber(parserTok* parser) {
	char number[MSG_SIZE];
	int digits = 0;

	while (strchr("0123456789", parser->tokens[parser->index]) && digits < MSG_SIZE && parser->index < parser->numTokens) {

		number[digits++] = parser->tokens[parser->index];
		parser->index++;
	}

	number[digits] = '\0';

	int value = atoi(number);

	expressionTree* numberExpression = createExpressionTree('n', value, NULL, NULL); // type 'n' as in number

	return numberExpression;
}

expressionTree* createExpressionTree(char type, int val, expressionTree* left, expressionTree* right) {
	expressionTree* expression = malloc(sizeof(expressionTree));

	expression->type = type;
	expression->val = val;
	expression->left = left;
	sexpression->right = right;

	return expression;
}

void freeExpressionTree(expressionTree* expression) {
	if (expression) {
		if (expression->left) freeExpressionTree(expression->left);
		if (expression->right) freeExpressionTree(expression->right);

		free(expression);
	}
}